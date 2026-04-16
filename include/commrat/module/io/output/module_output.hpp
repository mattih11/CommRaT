#pragma once

#include "commrat/messages.hpp"
#include "commrat/messaging/message_registry.hpp"
#include "commrat/messaging/message_helpers.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/mailbox/timestamped_ring_buffer.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/messaging/system/data_request_messages.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/platform/timestamp.hpp"
#include <optional>
#include <stdexcept>

namespace commrat {
//TODO: move to correct place
using SystemId = uint32_t;
using InstanceId = uint32_t;

/**
 * @brief Module output with timestamped buffering
 * 
 * Every output is buffered to support:
 * - publish() sends to all ContinuousInput subscribers immediately
 * - get_data(timestamp) queries from SyncedInput consumers
 * - Temporal synchronization for multi-rate sensor fusion
 * 
 * Provides command mailbox for receiving:
 * - System commands: SubscribeRequest, UnsubscribeRequest
 * - User-defined commands: Type-specific (SetRate, SetFilter, etc.)
 * 
 * Sends:
 * - SubscribeReply, UnsubscribeReply to requester's work_mbx
 * - Data to subscriber's data_mbx (published or via get_data response)
 * 
 * Address: calculate_cmd_mailbox_address(system_id, instance_id, type_id)
 * Must be per-output because address depends on output type_id
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 */
template<typename CommratApp, typename T, std::size_t SLOTS = 100>
class ModuleOutput
{
public:
    using Type = T;
    using ConfigType = OutputConfig;
    
    // Type introspection for infrastructure helpers
    using DataMessage = Message::Data<T>;
    using message_def_type = DataMessage;  // Alias for command_handler compatibility
    
    // CMD mailbox handles system commands and data request messages
    using CmdMailbox = TypedMailbox<CommratApp,
        SubscribeRequestPayload,
        SubscribeReplyPayload,
        UnsubscribeRequestPayload,
        UnsubscribeReplyPayload,
        GetDataRequestPayload<T>,
        GetDataReplyPayload<T>,
        GetNextDataRequestPayload<T>,
        GetNextDataReplyPayload<T>
        // TODO: Add user commands from DataWithCommands<T, Cmds...>
    >;
    
    // PUBLISH mailbox sends output data to subscribers (send-only)
    using PublishMailbox = TypedMailbox<CommratApp, T>;
    
    /**
     * @brief Default constructor (for tuple initialization)
     * 
     * Creates uninitialized output. Must call initialize() before use.
     */
    ModuleOutput()
        : system_id_(0)
        , instance_id_(0)
        , cmd_address_(0)
        , publish_address_(0)
        , cmd_mailbox_(std::nullopt)  // Explicitly initialize as empty
        , publish_mailbox_(std::nullopt)  // Explicitly initialize as empty
        , buffer_(Milliseconds(50))  // Default tolerance
        , default_tolerance_(Milliseconds(50))  // Duration type
    {}
    
    /**
     * @brief Construct output
     * @param system_id This module's system ID
     * @param instance_id This module's instance ID
     * @param default_tolerance Default tolerance for get_data queries
     */
    ModuleOutput(SystemId system_id, 
                 InstanceId instance_id,
                 Duration default_tolerance = Milliseconds(50))
        : system_id_(system_id)
        , instance_id_(instance_id)
        , cmd_address_(0)  // Set in initialize()
        , publish_address_(0)  // Set in initialize()
        , cmd_mailbox_(std::nullopt)  // Explicitly initialize as empty
        , publish_mailbox_(std::nullopt)  // Explicitly initialize as empty
        , buffer_(default_tolerance)
        , default_tolerance_(default_tolerance)
    {}
    
    /**
     * @brief Initialize output (allocation phase - NOT real-time safe)
     * 
     * Must be called before using the output. Creates CMD and PUBLISH mailboxes
     * with addresses based on this output's type_id.
     * Does NOT start mailboxes - call start() separately.
     * 
     * @param sys_id Module's system ID
     * @param inst_id Module's instance ID
     * @param tolerance Default tolerance for get_data queries
     */
    void initialize(uint8_t sys_id, uint8_t inst_id, Duration tolerance) {
        system_id_ = sys_id;
        instance_id_ = inst_id;
        default_tolerance_ = tolerance;
        
        // Calculate CMD mailbox address for this output
        // Uses output's type_id to make address unique per output type
        cmd_address_ = get_mailbox_address<T, std::tuple<>, CommratApp>(
            sys_id, inst_id, CMD_MBX_BASE
        );
        
        // Create CMD mailbox (allocation only, no start)
        MailboxConfig cmd_config{
            .mailbox_id = cmd_address_,
            .message_slots = 10,  // Commands are infrequent
            .max_message_size = CmdMailbox::max_message_size,
            .send_priority = 50,
            .realtime = true
        };
        
        cmd_mailbox_.emplace(cmd_config);
        
        // Calculate PUBLISH mailbox address (PUBLISH mailbox base)
        // This is for SENDING output data to subscribers
        publish_address_ = get_mailbox_address<T, std::tuple<>, CommratApp>(
            sys_id, inst_id, PUBLISH_MBX_BASE
        );
        
        // Create PUBLISH mailbox (allocation only, no start)
        MailboxConfig publish_config{
            .mailbox_id = publish_address_,
            .message_slots = 50,  // May send to multiple subscribers
            .max_message_size = PublishMailbox::max_message_size,
            .send_priority = 50,
            .realtime = true
        };
        
        publish_mailbox_.emplace(publish_config);
    }
    
    /**
     * @brief Start mailboxes (activation phase - real-time safe)
     * 
     * Activates CMD and PUBLISH mailboxes. Must be called after initialize().
     * Real-time safe: no allocations, only activates existing resources.
     * 
     * @throws std::runtime_error if mailboxes not initialized or TiMS start fails
     */
    void start() {
        if (!cmd_mailbox_ || !publish_mailbox_) {
            throw std::runtime_error("ModuleOutput not initialized - call initialize() first");
        }
        
        auto cmd_result = cmd_mailbox_->start();
        if (!cmd_result) {
            throw std::runtime_error("Failed to start CMD mailbox: TiMS initialization failed");
        }
        
        auto publish_result = publish_mailbox_->start();
        if (!publish_result) {
            cmd_mailbox_->stop();  // Cleanup on failure
            throw std::runtime_error("Failed to start PUBLISH mailbox: TiMS initialization failed");
        }
    }
    
    /**
     * @brief Stop mailboxes (deactivation phase - real-time safe)
     * 
     * Deactivates CMD and PUBLISH mailboxes without deallocating them.
     * Real-time safe: no allocations or syscalls.
     */
    void stop() {
        if (cmd_mailbox_) {
            cmd_mailbox_->stop();
        }
        if (publish_mailbox_) {
            publish_mailbox_->stop();
        }
    }
    
    /**
     * @brief Get CMD mailbox for this output
     * 
     * Used by Module2's command thread to receive commands for this output.
     * 
     * @return Reference to CMD mailbox
     */
    CmdMailbox& get_cmd_mailbox() {
        if (!cmd_mailbox_) {
            throw std::runtime_error("CMD mailbox not initialized - call initialize() first");
        }
        return *cmd_mailbox_;
    }
    
    /**
     * @brief Get CMD mailbox address for this output
     */
    uint32_t get_cmd_address() const {
        return cmd_address_;
    }
    
    /**
     * @brief Publish data by move (zero-copy when possible)
     * 
     * Sends data to all continuous subscribers based on their reduction settings.
     * One-shot subscribers (get_next_data) are removed after delivery.
     * 
     * @param data Data to publish (will be moved)
     * @param timestamp Timestamp for this data
     */
    void publish(T&& data, Timestamp timestamp) {
        TimsMessage<T> msg{
            .header = {
                .msg_type = CommratApp::template get_message_id<T>(),
                .msg_size = 0,
                .timestamp = timestamp,
                .seq_number = next_seq_number_++,
                .dest = 0,
                .src = 0,
                .flags = 0
            },
            .payload = std::move(data)
        };
        
        // Push to buffer first (for get_data queries)
        buffer_.push(msg);  // Copy before sending to subscribers
        global_data_count_++;
        
        // Send to subscribers with reduction using PUBLISH mailbox
        if (publish_mailbox_ && !continuous_subscribers_.empty()) {
            for (auto& subscriber : continuous_subscribers_) {
                if ((global_data_count_ % subscriber.reduction) == 0) {
                    // Send to subscriber's DATA mailbox via PUBLISH mailbox
                    publish_mailbox_->send(msg.payload, subscriber.data_mbx_addr, msg.header.timestamp);
                    
                    // Mark one-shot subscribers for removal
                    if (subscriber.get_next_data) {
                        subscriber.reduction = 0;  // Mark for removal
                    }
                }
            }
        }
        
        // Remove one-shot listeners
        continuous_subscribers_.erase(
            std::remove_if(continuous_subscribers_.begin(), continuous_subscribers_.end(),
                [](const SubscriberInfo& s) { return s.reduction == 0; }),
            continuous_subscribers_.end()
        );
    }
    
    /**
     * @brief Publish data by const reference (copy required)
     */
    void publish(const T& data, Timestamp timestamp) {
        publish(T{data}, timestamp);
    }
    
    /**
     * @brief Get workspace for zero-copy writing (RACK-style getDataBufferWorkSpace)
     * 
     * User writes directly to this buffer, then calls publish_workspace() to
     * move it into the ring buffer and send to subscribers.
     * 
     * @return Reference to workspace buffer (writable)
     * 
     * Example:
     * @code
     * auto& data = output.get_workspace();
     * data.value = read_sensor();
     * data.timestamp = Time::now().nanoseconds();
     * output.publish_workspace(Time::now());
     * @endcode
     */
    T& get_workspace() { 
        return workspace_; 
    }
    
    /**
     * @brief Publish workspace buffer (zero-copy)
     * 
     * Moves workspace into ring buffer and sends to subscribers.
     * After this call, workspace is in moved-from state - should be
     * overwritten before next use.
     * 
     * @param timestamp Timestamp for this data
     */
    void publish_workspace(Timestamp timestamp) {
        publish(std::move(workspace_), timestamp);
    }
    
    /**
     * @brief Get data synchronized to timestamp (RACK-style get_data)
     * 
     * Returns const reference to buffer slot - zero-copy read.
     * 
     * @param timestamp Requested timestamp (ns since epoch)
     * @param tolerance Maximum time difference (default: constructor value)
     * @param mode Interpolation strategy (NEAREST, BEFORE, AFTER)
     * @return Const pointer to message if found, nullptr otherwise
     * 
     * @note Thread-safe: Can be called concurrently with put_workspace()
     * @note O(log n) for BEFORE/AFTER, O(n) for NEAREST
     * @warning Returned pointer invalidated on next put_workspace() (ring buffer wrap)
     */
    const TimsMessage<T>* get_data(
        uint64_t timestamp,
        Duration tolerance = Duration::milliseconds(-1),
        InterpolationMode mode = InterpolationMode::NEAREST
    ) const {
        // Use default tolerance if not specified
        auto actual_tolerance = tolerance.is_negative() ? default_tolerance_ : tolerance;
        return buffer_.get_data(timestamp, actual_tolerance, mode);
    }
    
    /**
     * @brief Get timestamp range currently buffered
     * @return {oldest_timestamp, newest_timestamp} or {0, 0} if empty
     */
    std::pair<uint64_t, uint64_t> get_timestamp_range() const {
        return buffer_.get_timestamp_range();
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
     * Called by framework when SubscribeRequest received
     */
    auto handle_subscribe_request(const SubscribeRequestPayload& req) -> SubscribeReplyPayload {
        // Calculate reduction: how many messages to skip between sends
        uint32_t reduction = 1;
        if (req.requested_period_ms > 0 && req.requested_period_ms > period_ms_) {
            reduction = req.requested_period_ms / period_ms_;
        }
        
        // Add or update subscriber
        auto it = std::find_if(continuous_subscribers_.begin(), continuous_subscribers_.end(),
            [&](const SubscriberInfo& s) { return s.data_mbx_addr == req.subscriber_addr; });
        
        if (it != continuous_subscribers_.end()) {
            // Update existing
            it->requested_period_ms = req.requested_period_ms;
            it->reduction = reduction;
            it->get_next_data = (req.requested_period_ms < 0);  // Negative = one-shot
        } else if (continuous_subscribers_.size() < kMaxSubscribers) {
            // Add new
            continuous_subscribers_.push_back(SubscriberInfo{
                .data_mbx_addr = req.subscriber_addr,
                .requested_period_ms = req.requested_period_ms,
                .reduction = reduction,
                .get_next_data = (req.requested_period_ms < 0)
            });
        } else {
            return SubscribeReplyPayload{
                .actual_period_ms = 0,
                .success = false,
                .error_code = 1  // Max subscribers reached
            };
        }
        
        return SubscribeReplyPayload{
            .actual_period_ms = reduction * period_ms_,
            .success = true,
            .error_code = 0
        };
    }
    
    /**
     * @brief Process unsubscribe request
     */
    auto handle_unsubscribe_request(const UnsubscribeRequestPayload& req) -> UnsubscribeReplyPayload {
        continuous_subscribers_.erase(
            std::remove_if(continuous_subscribers_.begin(), continuous_subscribers_.end(),
                [&](const SubscriberInfo& s) { return s.data_mbx_addr == req.subscriber_addr; }),
            continuous_subscribers_.end()
        );
        
        return UnsubscribeReplyPayload{
            .success = true
        };
    }
    
    /**
     * @brief Process get_data request (timestamp-synchronized data fetch)
     * 
     * Called when consumer sends GetDataRequest to fetch historical data
     * matching a specific timestamp. Searches buffer and returns matching data.
     * 
     * @param req Request payload with target timestamp and search parameters
     * @return Reply payload with matched data or not-found indicator
     */
    auto handle_get_data_request(const GetDataRequestPayload<T>& req) -> GetDataReplyPayload<T> {
        // Convert interpolation mode from uint8_t to enum
        InterpolationMode mode = static_cast<InterpolationMode>(
            std::min(req.interpolation_mode, static_cast<uint8_t>(3))
        );
        
        // Convert tolerance from nanoseconds to Duration
        Duration tolerance_ms = req.tolerance_ns > 0 
            ? Duration::nanoseconds(static_cast<int64_t>(req.tolerance_ns))
            : Duration::milliseconds(-1);  // Use default
        
        // Search buffer for matching data
        const TimsMessage<T>* found = buffer_.get_data(
            req.target_timestamp, 
            tolerance_ms, 
            mode
        );
        
        if (found) {
            // Calculate actual time difference
            int64_t delta = static_cast<int64_t>(found->header.timestamp - req.target_timestamp);
            
            return GetDataReplyPayload<T>{
                .data = found->payload,
                .found = true,
                .data_timestamp = found->header.timestamp,
                .timestamp_delta_ns = delta
            };
        } else {
            return GetDataReplyPayload<T>{
                .data = {},
                .found = false,
                .data_timestamp = 0,
                .timestamp_delta_ns = 0
            };
        }
    }
    
    /**
     * @brief Process get_next_data request (one-shot data fetch)
     * 
     * Called when consumer sends GetNextDataRequest to fetch the next
     * available message from buffer (latest data).
     * 
     * @param req Request payload (empty - just requests latest)
     * @return Reply payload with latest data or not-found indicator
     */
    auto handle_get_next_data_request(const GetNextDataRequestPayload<T>& req) -> GetNextDataReplyPayload<T> {
        (void)req;  // Unused - request is empty
        
        // Get timestamp range to check if buffer has data
        auto [oldest_ts, newest_ts] = buffer_.get_timestamp_range();
        
        if (newest_ts == 0) {
            // Buffer is empty
            return GetNextDataReplyPayload<T>{
                .data = {},
                .found = false,
                .data_timestamp = 0
            };
        }
        
        // Get latest message (newest timestamp)
        const TimsMessage<T>* latest = buffer_.get_data(
            newest_ts,
            Milliseconds(0),  // Exact match tolerance
            InterpolationMode::NEAREST
        );
        
        if (latest) {
            return GetNextDataReplyPayload<T>{
                .data = latest->payload,
                .found = true,
                .data_timestamp = latest->header.timestamp
            };
        } else {
            return GetNextDataReplyPayload<T>{
                .data = {},
                .found = false,
                .data_timestamp = 0
            };
        }
    }
    
private:
    SystemId system_id_;
    InstanceId instance_id_;
    uint32_t cmd_address_;                                          // This output's command mailbox address
    uint32_t publish_address_;                                      // This output's publish mailbox address
    std::optional<CmdMailbox> cmd_mailbox_ = std::nullopt;          // Command mailbox for this output (default empty)
    std::optional<PublishMailbox> publish_mailbox_ = std::nullopt;  // Publish mailbox for sending data (default empty)
    OutputBuffer<TimsMessage<T>, SLOTS> buffer_;                    // Timestamped data storage
    Duration default_tolerance_;                                     // Default tolerance for get_data
    uint32_t next_seq_number_ = 0;                                  // Sequence counter
    T workspace_{};                                                 // Zero-copy workspace for user writes

    static constexpr std::size_t kMaxSubscribers = 16;
    
    struct SubscriberInfo {
        uint32_t data_mbx_addr;           ///< Subscriber's DATA mailbox address
        int64_t requested_period_ms;      ///< Requested update period
        uint32_t reduction;               ///< Send every Nth message (period ratio)
        bool get_next_data;               ///< One-shot mode: send once then remove
    };
    //TODO use sertial container
    sertial::fixed_vector<SubscriberInfo, kMaxSubscribers> continuous_subscribers_;
    uint64_t global_data_count_{0};       ///< Total messages published (for reduction)
    int64_t period_ms_{100};              ///< Actual output period in ms
};

}  // namespace commrat
