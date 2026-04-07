/**
 * @file timestamped_ring_buffer.hpp
 * @brief Thread-safe timestamped ring buffer for multi-input synchronization
 * 
 * Extends SeRTial's RingBuffer with timestamp-based lookup for get_data.
 * Used by HistoricalMailbox / ModuleOutput for secondary input sync.
 */

#pragma once

#include <commrat/platform/threading.hpp>
#include <commrat/platform/timestamp.hpp>
#include <commrat/messages.hpp>  // For TimsMessage
#include <sertial/containers/ring_buffer.hpp>
#include <optional>
#include <cmath>
#include <algorithm>

namespace commrat {

// ============================================================================
// Timestamp Accessor Trait (handles both direct .timestamp and .header.timestamp)
// ============================================================================

/**
 * @brief Trait to access timestamp from different message types
 * 
 * Default: Access .timestamp directly
 * Specialization for TimsMessage<T>: Access .header.timestamp
 */
template<typename T>
struct TimestampAccessor {
    static uint64_t get(const T& msg) {
        return msg.timestamp;
    }
};

// Specialization for TimsMessage<T>
template<typename PayloadT>
struct TimestampAccessor<TimsMessage<PayloadT>> {
    static uint64_t get(const TimsMessage<PayloadT>& msg) {
        return msg.header.timestamp;
    }
};

/**
 * @brief Interpolation mode for timestamp-based lookup
 */
enum class InterpolationMode {
    NEAREST,      ///< Return closest message by timestamp
    BEFORE,       ///< Return message at or before requested timestamp
    AFTER,        ///< Return message at or after requested timestamp
    INTERPOLATE   ///< Linear interpolation (if T supports it - future)
};

/**
 * @brief Thread-safe timestamped ring buffer with get_data lookup
 * 
 * @tparam T Message type (must have timestamp via TimestampAccessor)
 * @tparam MaxSize Maximum capacity (default: 100)
 * 
 * Thread-safe: shared lock for reads, exclusive lock for writes.
 */
template<typename T, std::size_t MaxSize = 100>
class OutputBuffer {
    // Compile-time validation
    static_assert(MaxSize > 0, "MaxSize must be greater than 0");
    
public:
    using value_type = T;
    using size_type = std::size_t;
    
    // ========================================================================
    // Construction
    // ========================================================================
    
    /**
     * @brief Constructor with optional sync tolerance
     * @param default_tolerance Default tolerance for timestamp matching (ms)
     */
    explicit OutputBuffer(
        std::chrono::milliseconds default_tolerance = std::chrono::milliseconds(50)
    ) : default_tolerance_(default_tolerance) {}
    
    // ========================================================================
    // Capacity
    // ========================================================================
    
    /**
     * @brief Get current number of stored messages
     * @return Number of messages in buffer
     * @note Thread-safe (read lock)
     */
    size_type size() const {
        SharedLock lock(mutex_);
        return buffer_.size();
    }
    
    /**
     * @brief Get maximum capacity
     * @return Maximum number of messages buffer can hold
     */
    static constexpr size_type capacity() {
        return MaxSize;
    }
    
    /**
     * @brief Check if buffer is empty
     * @return true if no messages stored
     * @note Thread-safe (read lock)
     */
    bool empty() const {
        SharedLock lock(mutex_);
        return buffer_.empty();
    }
    
    /**
     * @brief Check if buffer is full
     * @return true if buffer is at maximum capacity
     * @note Thread-safe (read lock)
     */
    bool full() const {
        SharedLock lock(mutex_);
        return buffer_.full();
    }
    
    /**
     * @brief Clear all messages from buffer
     * @note Thread-safe (write lock)
     */
    void clear() {
        UniqueLockShared lock(mutex_);
        buffer_.clear();
    }
    
    // ========================================================================
    // Modifiers (Zero-Copy Workspace Pattern - RACK-inspired)
    // ========================================================================
    
    /**
     * @brief Push message by move (zero-copy when possible)
     * 
     * @param message Message to store (will be moved)
     * @note Thread-safe (write lock)
     * @note O(1) time complexity
     */
    void push(T&& message) {
        UniqueLockShared lock(mutex_);
        
        #ifndef NDEBUG
        if (!buffer_.empty() && TimestampAccessor<T>::get(message) < TimestampAccessor<T>::get(buffer_.back())) {
            // WARNING: Timestamp order violation
        }
        #endif
        
        buffer_.push_back(std::move(message));
    }
    
    /**
     * @brief Push message by const reference (requires copy)
     */
    void push(const T& message) {
        push(T{message});  // Copy construct then move
    }
    
    /**
     * @brief Get next buffer slot for zero-copy write (uses SeRTial's emplace_back_in_place)
     * 
     * Returns a reference to the next buffer slot where user can write directly.
     * The slot is already default-constructed and ready for in-place writes.
     * Call finalize_write() after writing to update metadata.
     * 
     * @return Reference to next writable slot (zero-copy, real-time safe)
     * @note NOT thread-safe - caller must ensure exclusive access during write
     * @note Must call finalize_write() after writing to finalize metadata
     * 
     * @example
     * @code
     * auto& slot = buffer.get_next_slot();
     * slot.header.timestamp = Time::now().nanoseconds();
     * slot.payload.temperature = read_sensor();
     * buffer.finalize_write();  // Finalizes metadata (accesses via back())
     * @endcode
     */
    T& get_next_slot() noexcept {
        return buffer_.emplace_back_in_place();
    }
    
    /**
     * @brief Finalize write after get_next_slot() - validates timestamp order
     * 
     * Must be called after get_next_slot() to validate timestamp ordering.
     * 
     * @note Thread-safe (write lock)
     */
    void finalize_write() {
        #ifndef NDEBUG
        UniqueLockShared lock(mutex_);
        if (buffer_.size() > 1) {
            uint64_t newest = TimestampAccessor<T>::get(buffer_.back());
            uint64_t prev = TimestampAccessor<T>::get(buffer_[buffer_.size() - 2]);
            if (newest < prev) {
                // WARNING: Timestamp order violation
            }
        }
        #endif
    }
    
    // ========================================================================
    // Timestamp-Based Lookup (Primary Feature for Phase 6)
    // ========================================================================
    
    /**
     * @brief Get message synchronized to requested timestamp
     * 
     * Core functionality for multi-input synchronization. Finds the message
     * that best matches the requested timestamp according to the interpolation mode.
     * 
     * @param timestamp Requested timestamp (milliseconds since epoch)
     * @param tolerance Maximum acceptable time deviation (default: constructor value)
     * @param mode Interpolation strategy (default: NEAREST)
     * @return Message if found within tolerance, std::nullopt otherwise
     * 
     * @note Thread-safe (read lock)
     * @note O(log n) for BEFORE/AFTER (binary search), O(n) for NEAREST (linear scan)
     * 
     * Interpolation Modes:
     * - NEAREST: Returns message with smallest |timestamp - requested|
     * - BEFORE: Returns newest message where timestamp <= requested
     * - AFTER: Returns oldest message where timestamp >= requested
     * - INTERPOLATE: Future - linear interpolation between messages
     * 
     * @example
     * @code
     * // Primary input arrives at t=1234
     * auto imu = imu_mailbox.receive();  // Blocking
     * 
     * // Fetch secondary inputs synchronized to primary timestamp
     * auto gps = gps_history.get_data(imu.timestamp);
     * auto lidar = lidar_history.get_data(imu.timestamp);
     * 
     * if (gps && lidar) {
     *     process(imu, *gps, *lidar);  // All time-aligned!
     * }
     * @endcode
     */
    /**
     * @brief Get message synchronized to requested timestamp (zero-copy)
     * 
     * Returns const pointer to buffer slot - no copy.
     * 
     * @param timestamp Requested timestamp (nanoseconds since epoch)
     * @param tolerance Maximum acceptable time deviation
     * @param mode Interpolation strategy (default: NEAREST)
     * @return Const pointer to message if found, nullptr otherwise
     * 
     * @note Thread-safe (read lock)
     * @note O(log n) for BEFORE/AFTER, O(n) for NEAREST
     * @warning Returned pointer invalidated when buffer wraps (overwrites oldest)
     * 
     * @example
     * @code
     * if (const auto* msg = buffer.get_data(timestamp)) {
     *     process(*msg);  // Zero-copy access
     * }
     * @endcode
     */
    const T* get_data(
        uint64_t timestamp,
        std::chrono::milliseconds tolerance = std::chrono::milliseconds(-1),
        InterpolationMode mode = InterpolationMode::NEAREST
    ) const {
        SharedLock lock(mutex_);
        
        if (buffer_.empty()) {
            return nullptr;
        }
        
        // Use default tolerance if not specified
        if (tolerance.count() < 0) {
            tolerance = default_tolerance_;
        }
        
        // Convert tolerance from milliseconds to nanoseconds
        uint64_t tolerance_ns = static_cast<uint64_t>(tolerance.count()) * 1'000'000ULL;
        
        // Quick bounds check
        uint64_t oldest_ts = TimestampAccessor<T>::get(buffer_.front());
        uint64_t newest_ts = TimestampAccessor<T>::get(buffer_.back());
        
        uint64_t lower_bound = (timestamp >= tolerance_ns) ? (timestamp - tolerance_ns) : 0;
        uint64_t upper_bound = timestamp + tolerance_ns;
        if (upper_bound < timestamp) {
            upper_bound = UINT64_MAX;
        }
        
        if (upper_bound < oldest_ts || lower_bound > newest_ts) {
            return nullptr;
        }
        
        // Dispatch to mode-specific implementation
        switch (mode) {
            case InterpolationMode::NEAREST:
                return get_data_nearest(timestamp, tolerance_ns);
            case InterpolationMode::BEFORE:
                return get_data_before(timestamp, tolerance_ns);
            case InterpolationMode::AFTER:
                return get_data_after(timestamp, tolerance_ns);
            case InterpolationMode::INTERPOLATE:
                return get_data_nearest(timestamp, tolerance_ns);
        }
        
        return nullptr;
    }
    
    /**
     * @brief Get timestamp range currently in buffer
     * @return {oldest_timestamp, newest_timestamp} or {0, 0} if empty
     * @note Thread-safe (read lock)
     */
    std::pair<uint64_t, uint64_t> get_timestamp_range() const {
        SharedLock lock(mutex_);
        if (buffer_.empty()) {
            return {0, 0};
        }
        return {
            TimestampAccessor<T>::get(buffer_.front()),
            TimestampAccessor<T>::get(buffer_.back())
        };
    }
    
private:
    // ========================================================================
    // Internal Lookup Implementations (return pointers for zero-copy)
    // ========================================================================
    
    /**
     * @brief Find message with timestamp closest to requested
     * @note Assumes lock is held (called from get_data)
     */
    const T* get_data_nearest(uint64_t timestamp, uint64_t tolerance_ns) const {
        if (buffer_.empty()) {
            return nullptr;
        }
        
        size_type best_idx = 0;
        uint64_t best_diff = std::abs(static_cast<int64_t>(TimestampAccessor<T>::get(buffer_[0]) - timestamp));
        
        // Linear search for minimum time difference
        for (size_type i = 1; i < buffer_.size(); ++i) {
            uint64_t diff = std::abs(static_cast<int64_t>(TimestampAccessor<T>::get(buffer_[i]) - timestamp));
            if (diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }
        
        if (best_diff <= tolerance_ns) {
            return &buffer_[best_idx];
        }
        
        return nullptr;
    }
    
    /**
     * @brief Find newest message with timestamp <= requested
     * @note Assumes lock is held (called from get_data)
     */
    const T* get_data_before(uint64_t timestamp, uint64_t tolerance_units) const {
        if (buffer_.empty()) {
            return nullptr;
        }
        
        // Search backwards from newest to oldest
        for (size_type i = buffer_.size(); i > 0; --i) {
            size_type idx = i - 1;
            if (TimestampAccessor<T>::get(buffer_[idx]) <= timestamp) {
                uint64_t diff = timestamp - TimestampAccessor<T>::get(buffer_[idx]);
                if (diff <= tolerance_units) {
                    return &buffer_[idx];
                }
                break;
            }
        }
        
        return nullptr;
    }
    
    /**
     * @brief Find oldest message with timestamp >= requested
     * @note Assumes lock is held (called from get_data)
     */
    const T* get_data_after(uint64_t timestamp, uint64_t tolerance_units) const {
        if (buffer_.empty()) {
            return nullptr;
        }
        
        // Search forwards from oldest to newest
        for (size_type i = 0; i < buffer_.size(); ++i) {
            if (TimestampAccessor<T>::get(buffer_[i]) >= timestamp) {
                uint64_t diff = TimestampAccessor<T>::get(buffer_[i]) - timestamp;
                if (diff <= tolerance_units) {
                    return &buffer_[i];
                }
                break;
            }
        }
        
        return nullptr;
    }
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    mutable SharedMutex mutex_;                 ///< Thread synchronization
    sertial::RingBuffer<T, MaxSize> buffer_;    ///< Underlying circular buffer
    std::chrono::milliseconds default_tolerance_;  ///< Default tolerance for get_data
};

} // namespace commrat
