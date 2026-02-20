#pragma once

#include "commrat/commrat.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/mailbox/timestamped_ring_buffer.hpp"
#include "commrat/timestamp.hpp"
#include <optional>

/**
 * @brief Buffered output - stores timestamped data for getData queries
 * 
 * Combines functionality from HistoricalMailbox's buffer handling with
 * clean separation: storage (TimestampedRingBuffer) vs communication (CommandMailbox)
 * 
 * Stores recent outputs to support:
 * - publish() sends to all ContinuousInput subscribers immediately
 * - getData(timestamp) queries from SyncedInput consumers
 * - Temporal synchronization for multi-rate sensor fusion
 * 
 * Buffer is ONLY on output side (producer stores data)
 * Consumers never buffer - they either poll_data() or get_data(timestamp)
 * 
 * No tuple complexity - each BufferedOutput is strongly typed for single type T
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 * This is the clean separation: storage in output, communication via mailboxes
 */
template<typename CommratApp, typename T, std::size_t SLOTS = 100>
class BufferedOutput
    requires (is_commrat_message_v<T>),
             (CommratApp::UserRegistry::template is_registered<T>),
             (is_message_registry_v<typename CommratApp::UserRegistry>)
{
public:
    using Type = T;
    using CommandMailbox = TypedMailbox<typename CommratApp::SystemRegistry::SystemCommands, 
                                        typename CommratApp::UserRegistry::template UserCommands<T>>;
    
    /**
     * @brief Construct buffered output
     * @param system_id This module's system ID
     * @param instance_id This module's instance ID
     * @param default_tolerance Default tolerance for getData queries
     */
    BufferedOutput(SystemId system_id, 
                   InstanceId instance_id,
                   Milliseconds default_tolerance = Milliseconds(50))
        : system_id_(system_id)
        , instance_id_(instance_id)
        , cmd_address_(calculate_cmd_mailbox_address(system_id, 
                                                      instance_id, 
                                                      CommratApp::template get_message_id<T>()))
        , cmd_mbx_()  // TODO: Initialize with cmd_address_
        , buffer_(default_tolerance)
        , default_tolerance_(default_tolerance)
    {}
    
    /**
     * @brief Publish data to all continuous subscribers
     * 
     * Stores in buffer for getData queries AND sends to continuous subscribers.
     * 
     * @param data Data to publish
     * @param timestamp Timestamp for this data
     * @return Success or error
     */
    auto publish(const T& data, Timestamp timestamp) -> MailboxResult<void> {
        // Create TimsMessage with header
        TimsMessage<T> msg{
            .header = {
                .timestamp = timestamp.nanoseconds(),
                .sequence_number = next_seq_number_++,
                .message_id = CommratApp::template get_message_id<T>(),
                // TODO: Fill other header fields (source_id, etc.)
            },
            .payload = data
        };
        
        // Store in timestamped buffer (for getData queries)
        buffer_.push(msg);
        
        // TODO: Send to all continuous subscribers' data_mbx
        // for (auto& subscriber : continuous_subscribers_) {
        //     subscriber.data_mbx.send(msg);
        // }
        
        return MailboxResult<void>::success();
    }
    
    /**
     * @brief Get data synchronized to timestamp (RACK-style getData)
     * 
     * Called by SyncedInput consumers to fetch data at specific timestamp.
     * Uses TimestampedRingBuffer's optimized search (binary for BEFORE/AFTER,
     * linear for NEAREST).
     * 
     * @param timestamp Requested timestamp (ns since epoch)
     * @param tolerance Maximum time difference (default: constructor value)
     * @param mode Interpolation strategy (NEAREST, BEFORE, AFTER)
     * @return Message if found within tolerance, nullopt otherwise
     * 
     * @note Thread-safe: Can be called concurrently with publish()
     * @note O(log n) for BEFORE/AFTER, O(n) for NEAREST
     */
    auto getData(
        uint64_t timestamp,
        Milliseconds tolerance = Milliseconds(-1),
        InterpolationMode mode = InterpolationMode::NEAREST
    ) const -> std::optional<TimsMessage<T>> {
        // Use default tolerance if not specified
        auto actual_tolerance = (tolerance.count() < 0) ? default_tolerance_ : tolerance;
        return buffer_.getData(timestamp, actual_tolerance, mode);
    }
    
    /**
     * @brief Get timestamp range currently buffered
     * @return {oldest_timestamp, newest_timestamp} or {0, 0} if empty
     */
    std::pair<uint64_t, uint64_t> getTimestampRange() const {
        return buffer_.getTimestampRange();
    }
    
    /**
     * @brief Get buffer size
     */
    std::size_t buffer_size() const { 
        return buffer_.size(); 
    }
    
    /**
     * @brief Clear buffer
     */
    void clear_buffer() { 
        buffer_.clear(); 
    }
    
    /**
     * @brief Process subscription request
     * Called by framework when SubscribeRequest received on cmd_mbx_
     */
    auto handle_subscribe_request(const SubscribeRequest& req) -> MailboxResult<void> {
        // TODO: Add subscriber to list, send SubscriptionAck to req.work_mbx_address
        return MailboxResult<void>::success();
    }
    
    /**
     * @brief Process unsubscribe request
     */
    auto handle_unsubscribe_request(const UnsubscribeRequest& req) -> MailboxResult<void> {
        // TODO: Remove subscriber from list
        return MailboxResult<void>::success();
    }
    
private:
    SystemId system_id_;
    InstanceId instance_id_;
    uint32_t cmd_address_;                                          // This output's command mailbox address
    CommandMailbox cmd_mbx_;                                        // Receives commands, sends acks
    TimestampedRingBuffer<TimsMessage<T>, SLOTS> buffer_;          // Timestamped data storage
    Milliseconds default_tolerance_;                                // Default tolerance for getData
    uint32_t next_seq_number_ = 0;                                  // Sequence counter
    
    // TODO: Subscriber list
    // std::vector<SubscriberInfo> continuous_subscribers_;  // ContinuousInput consumers
    // std::vector<SubscriberInfo> synced_subscribers_;      // SyncedInput consumers (getData only)
};
