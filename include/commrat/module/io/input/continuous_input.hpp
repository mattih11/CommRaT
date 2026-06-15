#pragma once

#include "cmd_input.hpp"
#include <corerat/ipc/mailbox.hpp>
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include <corerat/platform/timestamp.hpp>
#include <corerat/platform/duration.hpp>
#include <optional>

namespace commrat {

using corerat::Duration;
using corerat::Milliseconds;
using corerat::MailboxConfig;

/**
 * @brief Continuous input interface - receives push data stream (RACK RackDataProxy)
 * 
 * Receives data via push model from producer. Data arrives asynchronously
 * and is consumed immediately in process() without local buffering.
 * 
 * Flow (RACK getContData pattern):
 * 1. send_command(SubscribeRequest) to producer's CMD mailbox
 * 2. Producer sends SubscribeReply with actual_period_ms
 * 3. Producer publishes TimsMessage<T> to our DATA mailbox
 * 4. poll_data() receives from DATA mailbox (blocking with timeout)
 * 
 * NO local buffer - data goes directly to process()
 * Buffers exist only on producer side (ModuleOutput)
 * 
 * @tparam Registry Message registry
 * @tparam OutputType Output message type (e.g., SensorData) - determines type_id
 * 
 * RACK Equivalent: RackDataProxy::getContData() + stopContData()
 * Modern approach: Type-safe subscription, compile-time type_id
 */
template<typename Registry, typename OutputType>
class ContinuousInput : public CmdInput<Registry, OutputType> {
public:
    // Type introspection (matches ModuleOutput and SyncedInput interface)
    using DataMessage = Message::Data<OutputType>;
    using DataMailbox = TypedMailbox<Registry, OutputType>;
    
    /**
     * @brief Default constructor (for tuple initialization)
     * 
     * Creates uninitialized input. Must call initialize() before use.
     */
    ContinuousInput()
        : CmdInput<Registry, OutputType>()
        , data_mbx_(std::nullopt)
        , requested_period_(Duration::zero())
        , poll_timeout_(Milliseconds(100))
        , subscribed_(false)
        , actual_period_(Duration::zero())
    {}
    
    /**
     * @brief Initialize input with mailboxes and producer address
     * 
     * Call this after default construction to set up the input.
     * Creates DATA mailbox for receiving continuous stream.
     * Does NOT start mailbox - call start() separately.
     * 
     * @param work_mbx Shared work mailbox (for subscription protocol)
     * @param data_mbx_config Configuration for DATA mailbox (address, buffer sizes)
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     * @param requested_period Desired update period (0 = as fast as possible)
     * @param poll_timeout How long to wait for new data
     * @param cmd_timeout Timeout for subscription commands
     */
    void initialize(typename Registry::System::WorkMailbox& work_mbx,
                    const MailboxConfig& data_mbx_config,
                    uint8_t producer_system_id,
                    uint8_t producer_instance_id,
                    Duration requested_period = Duration::zero(),
                    Duration poll_timeout = Milliseconds(100),
                    Duration cmd_timeout = Milliseconds(1000)) {
        // Initialize base class
        CmdInput<Registry, OutputType>::initialize(work_mbx, producer_system_id, producer_instance_id, cmd_timeout);
        
        // Create DATA mailbox for receiving stream (allocation only, no start)
        data_mbx_.emplace(data_mbx_config);
        
        // Initialize our members
        requested_period_ = requested_period;
        poll_timeout_ = poll_timeout;
        subscribed_ = false;
        actual_period_ = Duration::zero();
    }
    
    /**
     * @brief Start DATA mailbox (activation phase - real-time safe)
     * 
     * Activates DATA mailbox. Must be called after initialize().
     * Real-time safe: no allocations, only activates existing resources.
     * 
     * @throws std::runtime_error if mailbox not initialized or TiMS start fails
     */
    void start() {
        if (!data_mbx_) {
            throw std::runtime_error("ContinuousInput not initialized - call initialize() first");
        }
        
        auto result = data_mbx_->start();
        if (!result) {
            throw std::runtime_error("Failed to start DATA mailbox: TiMS initialization failed");
        }
    }
    
    /**
     * @brief Stop DATA mailbox (deactivation phase - real-time safe)
     */
    void stop() {
        if (data_mbx_) {
            data_mbx_->stop();
        }
    }
    
    /**
     * @brief Construct continuous input
     * @param work_mbx Shared mailbox for subscription protocol (SubscribeReply)
     * @param data_mbx Dedicated mailbox for receiving data stream
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     * @param requested_period Desired update period (0 = as fast as possible)
     * @param poll_timeout How long to wait for new data in poll_data()
     * @param cmd_timeout Timeout for subscription commands
     */
    ContinuousInput(MailboxFor<Registry>& work_mbx,
                    MailboxFor<Registry>& data_mbx,
                    uint8_t producer_system_id,
                    uint8_t producer_instance_id,
                    Duration requested_period = Duration::zero(),
                    Duration poll_timeout = Milliseconds(100),
                    Duration cmd_timeout = Milliseconds(1000))
        : CmdInput<Registry, OutputType>(work_mbx, producer_system_id, producer_instance_id, cmd_timeout)
        , data_mbx_(&data_mbx)
        , requested_period_(requested_period)
        , poll_timeout_(poll_timeout)
        , subscribed_(false)
        , actual_period_(Duration::zero())
    {}
    
    /**
     * @brief Subscribe to producer's output (RACK getContData)
     * 
     * Sends SubscribeRequest to producer's CMD mailbox.
     * Producer replies with actual period and starts sending data.
     * 
     * @param actual_period Output parameter - producer's actual update period
     * @param timeout Subscription command timeout
     * @return True if subscription succeeded, false on error
     */
    bool subscribe(Duration& actual_period, Duration timeout = Duration::zero()) {
        // Build subscribe request
        TimsMessage<SubscribeRequestPayload> request{
            .header = {
                .msg_type = Registry::template get_message_id<SubscribeRequestPayload>(),
                .msg_size = 0,  // Will be set by serialization
                .timestamp = Time::now(),
                .seq_number = 0,
                .dest = 0,  // Will be set by subscribe()
                .src = data_mbx_->mailbox_id(),  // Producer sends data here
                .flags = 0
            },
            .payload = {
                .subscriber_addr = data_mbx_->mailbox_id(),
                .requested_period_ms = requested_period_.count_ms()
            }
        };
        
        // Send and receive reply
        TimsMessage<SubscribeReplyPayload> reply;
        if (!this->send_command(request, reply, timeout)) {
            return false;
        }
        
        // Check reply
        if (!reply.payload.success) {
            return false;
        }
        
        // Store actual period
        actual_period_ = Milliseconds(reply.payload.actual_period_ms);
        actual_period = actual_period_;
        subscribed_ = true;
        return true;
    }
    
    /**
     * @brief Unsubscribe from producer's output (RACK stopContData)
     * 
     * @param timeout Unsubscribe command timeout
     * @return True if unsubscribe succeeded, false on error
     */
    bool unsubscribe(Duration timeout = Duration::zero()) {
        if (!subscribed_) {
            return true;  // Already unsubscribed
        }
        
        // Build unsubscribe request
        TimsMessage<UnsubscribeRequestPayload> request{
            .header = {
                .msg_type = Registry::template get_message_id<UnsubscribeRequestPayload>(),
                .msg_size = 0,
                .timestamp = Time::now(),
                .seq_number = 0,
                .dest = 0,
                .src = data_mbx_->mailbox_id(),
                .flags = 0
            },
            .payload = {
                .subscriber_addr = data_mbx_->mailbox_id()
            }
        };
        
        // Send and receive reply
        TimsMessage<UnsubscribeReplyPayload> reply;
        if (!this->send_command(request, reply, timeout)) {
            return false;
        }
        
        subscribed_ = false;
        return reply.payload.success;
    }
    
    /**
     * @brief Poll for new data from producer (blocking receive - ZERO-COPY)
     * 
     * Receives directly into internal last_message_ buffer. No intermediate
     * copies or moves - true zero-copy from mailbox to storage!
     * Called by framework to fetch data for process() execution.
     * 
     * @return True if data received, false on timeout or error
     */
    bool poll_data() {
        // Mailbox receives DIRECTLY into our storage (zero-copy!)
        if (!data_mbx_->receive(last_message_, poll_timeout_)) {
            has_data_ = false;
            return false;
        }
        
        has_data_ = true;
        return true;
    }
    
    /**
     * @brief Get const reference to payload (zero-copy)
     * @return Reference to last received payload
     * @note Only valid if poll_data() succeeded
     */
    const OutputType& get_payload() const {
        return last_message_.payload;
    }
    
    /**
     * @brief Get const reference to header (zero-copy metadata access)
     * @return Reference to last received header
     * @note Only valid if poll_data() succeeded
     */
    const TimsHeader& get_header() const {
        return last_message_.header;
    }
    
    /**
     * @brief Get timestamp from header (convenience)
     * @return Timestamp in nanoseconds since epoch
     */
    uint64_t get_timestamp() const {
        return last_message_.header.timestamp;
    }
    
    /**
     * @brief Get sequence number from header (convenience)
     */
    uint32_t get_sequence_number() const {
        return last_message_.header.seq_number;
    }
    
    /**
     * @brief Get message type from header (convenience)
     */
    uint32_t get_message_type() const {
        return last_message_.header.msg_type;
    }
    
    /**
     * @brief Check if valid data available
     * @return True if last poll_data() succeeded
     */
    bool has_data() const {
        return has_data_;
    }
    
    /**
     * @brief Check if input is valid (unified metadata API)
     * @return True if last poll_data() succeeded
     * @note Alias for has_data() - provides consistent API with SyncedInput
     */
    bool is_valid() const {
        return has_data_;
    }
    
    /**
     * @brief Check if input has fresh data (unified metadata API)
     * @return Always true for continuous input (blocking receive is always fresh)
     * @note Provides consistent API with SyncedInput where staleness matters
     */
    bool is_fresh() const {
        return has_data_;  // Continuous input is fresh if valid
    }
    
    /**
     * @brief Check if subscribed to producer
     */
    bool is_subscribed() const { return subscribed_; }
    
    /**
     * @brief Get actual update period from producer
     */
    Duration get_actual_period() const { return actual_period_; }
    
private:
    std::optional<DataMailbox> data_mbx_;       ///< Owned DATA mailbox for receiving stream (optional for two-phase init)
    Duration requested_period_;             ///< Requested update period
    Duration poll_timeout_;                 ///< Timeout for poll_data()
    bool subscribed_;                           ///< Currently subscribed?
    Duration actual_period_;                ///< Actual period from producer
    TimsMessage<OutputType> last_message_{};    ///< Last received message (zero-copy storage)
    bool has_data_{false};                      ///< True if last_message_ valid
};

} // namespace commrat
