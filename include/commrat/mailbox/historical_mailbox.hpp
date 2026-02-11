/**
 * @file historical_mailbox.hpp
 * @brief RegistryMailbox wrapper with timestamped history for multi-input synchronization (Phase 6.3)
 * 
 * Extends RegistryMailbox with:
 * - Automatic history buffering for received messages
 * - getData(timestamp) for synchronized multi-input access
 * - Thread-safe publish and getData operations
 * 
 * Used by secondary inputs in multi-input modules to provide temporal
 * synchronization with primary input.
 * 
 * @author CommRaT Development Team
 * @date February 8, 2026
 */

#pragma once

#include "registry_mailbox.hpp"
#include "timestamped_ring_buffer.hpp"
#include "../platform/threading.hpp"
#include <optional>
#include <memory>

namespace commrat {

/**
 * @brief Mailbox with timestamped history for getData synchronization
 * 
 * Wraps RegistryMailbox and maintains a TimestampedRingBuffer for each
 * registered message type. Received messages are automatically stored
 * in history, enabling getData() queries for multi-input synchronization.
 * 
 * @tparam UserRegistry Message registry (MessageRegistry<...>)
 * @tparam HistorySize Maximum messages to buffer per type (default: 100)
 * 
 * Thread Safety:
 * - receive() and getData() can be called concurrently (reader-writer lock)
 * - Multiple getData() calls are lock-free readers
 * - receive() is exclusive writer
 * 
 * Usage Pattern:
 * @code
 * // Primary input (blocking - drives execution rate)
 * auto imu = imu_mailbox.receive<IMUData>();  
 * 
 * // Secondary inputs (non-blocking - fetch at primary timestamp)
 * auto gps = gps_history.getData<GPSData>(imu.timestamp);
 * auto lidar = lidar_history.getData<LidarData>(imu.timestamp);
 * 
 * if (gps && lidar) {
 *     process(imu, *gps, *lidar);  // All time-aligned!
 * }
 * @endcode
 * 
 * Architecture:
 * - One TimestampedRingBuffer per message type in registry
 * - Automatic deserialization on receive → store in history
 * - getData() queries appropriate buffer based on type
 * - FIFO overflow: oldest messages discarded when buffer full
 */
template<typename UserRegistry, std::size_t HistorySize = 100>
class HistoricalMailbox {
public:
    using Registry = UserRegistry;
    using MailboxType = RegistryMailbox<UserRegistry>;
    
    // Define history buffer tuple type here (where TimestampedRingBuffer is known)
    template<std::size_t... Is>
    static auto make_history_tuple_type(std::index_sequence<Is...>) {
        return std::tuple<
            std::unique_ptr<TimestampedRingBuffer<TimsMessage<typename Registry::template type_at<Is>>, HistorySize>>...
        >{};
    }
    
    using HistoryBufferTuple = decltype(make_history_tuple_type(
        std::make_index_sequence<Registry::num_types>{}
    ));
    
    // ========================================================================
    // Construction and Configuration
    // ========================================================================
    
    /**
     * @brief Constructor with mailbox config and default tolerance
     * @param config Mailbox configuration
     * @param default_tolerance Default tolerance for getData (milliseconds)
     */
    explicit HistoricalMailbox(
        const MailboxConfig& config,
        Milliseconds default_tolerance = Milliseconds(50)
    ) : mailbox_(config), default_tolerance_(default_tolerance) {
        // Initialize history buffers for all registered types
        init_history_buffers();
    }
    
    /**
     * @brief Start the mailbox (pass-through to underlying mailbox)
     * @return Success or error from underlying mailbox initialization
     */
    auto start() -> MailboxResult<void> {
        auto result = mailbox_.start();
        if (!result) {
            std::cerr << "[HistoricalMailbox] Start failed for mailbox " 
                      << mailbox_.mailbox_id() << " - error " 
                      << static_cast<int>(result.get_error()) << "\n";
        } else {
            std::cout << "[HistoricalMailbox] Started mailbox " 
                      << mailbox_.mailbox_id() << " successfully\n";
        }
        return result;
    }
    
    /**
     * @brief Stop the mailbox (pass-through to underlying mailbox)
     */
    void stop() {
        mailbox_.stop();
    }
    
    // ========================================================================
    // Primary Input API (Blocking Receive)
    // ========================================================================
    
    /**
     * @brief Receive a message and automatically store in history
     * 
     * Receives next message of type T, stores it in history, and returns it.
     * This is the PRIMARY input API - blocks until message arrives.
     * 
     * @tparam T Payload type (must be registered in UserRegistry)
     * @return Received message or error
     * 
     * @note Thread-safe: Exclusive write access to history buffer
     * @note Automatically stores in history for future getData() queries
     */
    template<typename T>
    auto receive() -> MailboxResult<TimsMessage<T>> {
        
        // Receive from underlying mailbox
        auto result = mailbox_.template receive<T>();
        
        if (result) {
            // Store in history for future getData queries
            store_in_history(result.value());
        }
        
        return result;
    }
    
    // ========================================================================
    // Secondary Input API (Non-Blocking getData)
    // ========================================================================
    
    /**
     * @brief Get message synchronized to requested timestamp (non-blocking)
     * 
     * Core API for secondary inputs in multi-input modules. Fetches message
     * from history that best matches the requested timestamp.
     * 
     * @tparam T Payload type (must be registered in UserRegistry)
     * @param timestamp Requested timestamp (ns since epoch)
     * @param tolerance Maximum acceptable time deviation (default: constructor value)
     * @param mode Interpolation strategy (default: NEAREST)
     * @return Message if found within tolerance, std::nullopt otherwise
     * 
     * @note Thread-safe: Shared read access (multiple concurrent getData allowed)
     * @note Non-blocking: Returns immediately with cached data
     * @note O(log n) for BEFORE/AFTER, O(n) for NEAREST
     * 
     * @example
     * @code
     * // Primary drives execution
     * auto imu = imu_mailbox.receive<IMUData>();
     * 
     * // Secondaries fetch at primary timestamp
     * auto gps = gps_history.getData<GPSData>(imu.timestamp);
     * auto lidar = lidar_history.getData<LidarData>(imu.timestamp);
     * @endcode
     */
    template<typename T>
    std::optional<TimsMessage<T>> getData(
        uint64_t timestamp,
        Milliseconds tolerance = Milliseconds(-1),
        InterpolationMode mode = InterpolationMode::NEAREST
    ) const {
        auto& buffer = get_history_buffer<T>();
        return buffer.getData(timestamp, tolerance, mode);
    }
    
    /**
     * @brief Get timestamp range currently buffered for type T
     * @return {oldest_timestamp, newest_timestamp} or {0, 0} if empty
     */
    template<typename T>
    std::pair<uint64_t, uint64_t> getTimestampRange() const {
        const auto& buffer = get_history_buffer<T>();
        return buffer.getTimestampRange();
    }
    
    /**
     * @brief Clear history for type T
     */
    template<typename T>
    void clearHistory() {
        auto& buffer = get_history_buffer<T>();
        buffer.clear();
    }
    
    /**
     * @brief Clear all history buffers
     */
    void clearAllHistory() {
        clear_all_buffers();
    }
    
    // ========================================================================
    // Pass-Through API to Underlying Mailbox
    // ========================================================================
    
    /**
     * @brief Send a message (pass-through to underlying mailbox)
     */
    template<typename T>
    auto send(T& message, uint32_t dest_mailbox) -> MailboxResult<void> {
        return mailbox_.template send(message, dest_mailbox);
    }
    
    /**
     * @brief Get mailbox ID
     */
    uint32_t get_mailbox_id() const {
        return mailbox_.mailbox_id();
    }
    
    /**
     * @brief Check if mailbox is initialized
     */
    bool is_initialized() const {
        return mailbox_.is_initialized();
    }
    
private:
    // ========================================================================
    // Internal Implementation
    // ========================================================================
    
    /**
     * @brief Store received message in history buffer
     */
    template<typename T>
    void store_in_history(const TimsMessage<T>& tims_msg) {
        auto& buffer = get_history_buffer<T>();
        
        // Store TimsMessage directly - no conversion needed!
        // Phase 6.10: Timestamp is in header (tims_msg.header.timestamp)
        buffer.push(tims_msg);
    }
    
    /**
     * @brief Get history buffer for type T (const version)
     */
    template<typename T>
    const TimestampedRingBuffer<TimsMessage<T>, HistorySize>& get_history_buffer() const {
        constexpr auto type_index = Registry::template get_type_index<T>();
        return *std::get<type_index>(history_buffers_);
    }
    
    /**
     * @brief Get history buffer for type T (mutable version)
     */
    template<typename T>
    TimestampedRingBuffer<TimsMessage<T>, HistorySize>& get_history_buffer() {
        constexpr auto type_index = Registry::template get_type_index<T>();
        return *std::get<type_index>(history_buffers_);
    }
    
    /**
     * @brief Initialize history buffers for all registered types
     */
    void init_history_buffers() {
        init_buffers_impl(std::make_index_sequence<Registry::num_types>{});
    }
    
    /**
     * @brief Initialize buffers using index sequence
     */
    template<std::size_t... Is>
    void init_buffers_impl(std::index_sequence<Is...>) {
        ((init_buffer<Is>()), ...);
    }
    
    /**
     * @brief Initialize buffer for type at index I
     */
    template<std::size_t I>
    void init_buffer() {
        using PayloadType = typename Registry::template type_at<I>;
        std::get<I>(history_buffers_) = 
            std::make_unique<TimestampedRingBuffer<TimsMessage<PayloadType>, HistorySize>>(
                default_tolerance_
            );
    }
    
    /**
     * @brief Clear all history buffers
     */
    void clear_all_buffers() {
        clear_all_impl(std::make_index_sequence<Registry::num_types>{});
    }
    
    template<std::size_t... Is>
    void clear_all_impl(std::index_sequence<Is...>) {
        ((std::get<Is>(history_buffers_)->clear()), ...);
    }
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    MailboxType mailbox_;  ///< Underlying registry mailbox
    
    Milliseconds default_tolerance_;  ///< Default tolerance for getData
    
    /**
     * @brief History buffer tuple - one per registered type
     * 
     * Uses unique_ptr for dynamic allocation (buffers are large).
     * Tuple indexing matches Registry::get_type_index<T>().
     */
    HistoryBufferTuple history_buffers_;
};

} // namespace commrat
