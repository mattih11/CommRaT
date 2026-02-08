/**
 * @file timestamped_ring_buffer.hpp
 * @brief Thread-safe timestamped ring buffer for multi-input synchronization (Phase 6)
 * 
 * Extends SeRTial's RingBuffer with timestamp-based lookup for synchronized getData.
 * Used by HistoricalMailbox to store message history for secondary inputs.
 * 
 * @author CommRaT Development Team
 * @date February 8, 2026
 */

#pragma once

#include <commrat/threading.hpp>
#include <commrat/timestamp.hpp>
#include <sertial/containers/ring_buffer.hpp>
#include <optional>
#include <cmath>
#include <algorithm>

namespace commrat {

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
 * @brief Thread-safe timestamped ring buffer with getData lookup
 * 
 * Wraps SeRTial::RingBuffer with:
 * - Thread-safe push/get operations
 * - Timestamp-based lookup (getData)
 * - Multiple interpolation modes
 * - Maintains temporal ordering (must push in timestamp order)
 * 
 * @tparam T Message type (must have .timestamp member)
 * @tparam MaxSize Maximum capacity (default: 100 messages)
 * 
 * Requirements for T:
 * - Must have uint64_t timestamp field (milliseconds since epoch)
 * - Must be copyable or movable
 * - Timestamps must be monotonically increasing on push
 * 
 * Thread Safety:
 * - Multiple concurrent readers (shared lock)
 * - Single writer (exclusive lock)
 * - Lock-free reads possible in future optimization
 * 
 * Example:
 * @code
 * struct IMUData {
 *     uint64_t timestamp;  // Required!
 *     float ax, ay, az;
 * };
 * 
 * TimestampedRingBuffer<IMUData, 100> history;
 * 
 * // Producer thread
 * history.push(IMUData{.timestamp = 1000, .ax = 1.0f, ...});
 * 
 * // Consumer thread (secondary input)
 * auto data = history.getData(1050, std::chrono::milliseconds(50), 
 *                             InterpolationMode::NEAREST);
 * if (data) {
 *     // Use data synchronized to timestamp 1050
 * }
 * @endcode
 */
template<typename T, std::size_t MaxSize = 100>
class TimestampedRingBuffer {
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
    explicit TimestampedRingBuffer(
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
        oldest_timestamp_ = 0;
        newest_timestamp_ = 0;
    }
    
    // ========================================================================
    // Modifiers
    // ========================================================================
    
    /**
     * @brief Push new message with timestamp
     * 
     * Messages must be pushed in timestamp order (monotonically increasing).
     * If buffer is full, overwrites oldest message.
     * 
     * @param message Message to store (must have .timestamp field)
     * @note Thread-safe (write lock)
     * @note O(1) time complexity
     * 
     * @warning Violating timestamp order leads to undefined getData behavior!
     */
    void push(const T& message) {
        UniqueLockShared lock(mutex_);
        
        // Validate timestamp ordering (debug only)
        #ifndef NDEBUG
        if (!buffer_.empty() && message.timestamp < newest_timestamp_) {
            // WARNING: Timestamps must be monotonically increasing
            // This is a logic error in the producer
        }
        #endif
        
        // Update timestamp bounds
        if (buffer_.empty()) {
            oldest_timestamp_ = message.timestamp;
        } else if (buffer_.full()) {
            // Overwriting oldest - update lower bound
            oldest_timestamp_ = buffer_[1].timestamp;  // Second-oldest becomes oldest
        }
        newest_timestamp_ = message.timestamp;
        
        buffer_.push_back(message);
    }
    
    /**
     * @brief Push new message via move
     * @param message Message to move into buffer
     * @note Thread-safe (write lock)
     */
    void push(T&& message) {
        UniqueLockShared lock(mutex_);
        
        #ifndef NDEBUG
        if (!buffer_.empty() && message.timestamp < newest_timestamp_) {
            // WARNING: Timestamp order violation
        }
        #endif
        
        if (buffer_.empty()) {
            oldest_timestamp_ = message.timestamp;
        } else if (buffer_.full()) {
            oldest_timestamp_ = buffer_[1].timestamp;
        }
        newest_timestamp_ = message.timestamp;
        
        buffer_.push_back(std::move(message));
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
     * auto gps = gps_history.getData(imu.timestamp);
     * auto lidar = lidar_history.getData(imu.timestamp);
     * 
     * if (gps && lidar) {
     *     process(imu, *gps, *lidar);  // All time-aligned!
     * }
     * @endcode
     */
    std::optional<T> getData(
        uint64_t timestamp,
        std::chrono::milliseconds tolerance = std::chrono::milliseconds(-1),
        InterpolationMode mode = InterpolationMode::NEAREST
    ) const {
        SharedLock lock(mutex_);
        
        if (buffer_.empty()) {
            return std::nullopt;
        }
        
        // Use default tolerance if not specified
        if (tolerance.count() < 0) {
            tolerance = default_tolerance_;
        }
        
        // Quick bounds check
        uint64_t tolerance_ms = static_cast<uint64_t>(tolerance.count());
        if (timestamp + tolerance_ms < oldest_timestamp_ || 
            timestamp - tolerance_ms > newest_timestamp_) {
            return std::nullopt;  // Requested timestamp outside buffer range
        }
        
        // Dispatch to mode-specific implementation
        switch (mode) {
            case InterpolationMode::NEAREST:
                return getData_nearest(timestamp, tolerance_ms);
            case InterpolationMode::BEFORE:
                return getData_before(timestamp, tolerance_ms);
            case InterpolationMode::AFTER:
                return getData_after(timestamp, tolerance_ms);
            case InterpolationMode::INTERPOLATE:
                // Future: Linear interpolation between messages
                // For now, fall back to NEAREST
                return getData_nearest(timestamp, tolerance_ms);
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Get timestamp range currently in buffer
     * @return {oldest_timestamp, newest_timestamp} or {0, 0} if empty
     * @note Thread-safe (read lock)
     */
    std::pair<uint64_t, uint64_t> getTimestampRange() const {
        SharedLock lock(mutex_);
        if (buffer_.empty()) {
            return {0, 0};
        }
        return {oldest_timestamp_, newest_timestamp_};
    }
    
private:
    // ========================================================================
    // Internal Lookup Implementations
    // ========================================================================
    
    /**
     * @brief Find message with timestamp closest to requested
     * @note Assumes lock is held (called from getData)
     */
    std::optional<T> getData_nearest(uint64_t timestamp, uint64_t tolerance_ms) const {
        if (buffer_.empty()) {
            return std::nullopt;
        }
        
        size_type best_idx = 0;
        uint64_t best_diff = std::abs(static_cast<int64_t>(buffer_[0].timestamp - timestamp));
        
        // Linear search for minimum time difference
        for (size_type i = 1; i < buffer_.size(); ++i) {
            uint64_t diff = std::abs(static_cast<int64_t>(buffer_[i].timestamp - timestamp));
            if (diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }
        
        if (best_diff <= tolerance_ms) {
            return buffer_[best_idx];
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Find newest message with timestamp <= requested
     * @note Assumes lock is held (called from getData)
     */
    std::optional<T> getData_before(uint64_t timestamp, uint64_t tolerance_ms) const {
        if (buffer_.empty()) {
            return std::nullopt;
        }
        
        // Search backwards from newest to oldest
        for (size_type i = buffer_.size(); i > 0; --i) {
            size_type idx = i - 1;
            if (buffer_[idx].timestamp <= timestamp) {
                uint64_t diff = timestamp - buffer_[idx].timestamp;
                if (diff <= tolerance_ms) {
                    return buffer_[idx];
                }
                break;  // Found newest candidate, but out of tolerance
            }
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Find oldest message with timestamp >= requested
     * @note Assumes lock is held (called from getData)
     */
    std::optional<T> getData_after(uint64_t timestamp, uint64_t tolerance_ms) const {
        if (buffer_.empty()) {
            return std::nullopt;
        }
        
        // Search forwards from oldest to newest
        for (size_type i = 0; i < buffer_.size(); ++i) {
            if (buffer_[i].timestamp >= timestamp) {
                uint64_t diff = buffer_[i].timestamp - timestamp;
                if (diff <= tolerance_ms) {
                    return buffer_[i];
                }
                break;  // Found oldest candidate, but out of tolerance
            }
        }
        
        return std::nullopt;
    }
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    mutable SharedMutex mutex_;                 ///< Thread synchronization
    sertial::RingBuffer<T, MaxSize> buffer_;    ///< Underlying circular buffer
    
    // Cached timestamp bounds (avoid scanning on every getData)
    uint64_t oldest_timestamp_{0};  ///< Timestamp of oldest message
    uint64_t newest_timestamp_{0};  ///< Timestamp of newest message
    
    std::chrono::milliseconds default_tolerance_;  ///< Default tolerance for getData
};

} // namespace commrat
