#pragma once

#include "cmd_input.hpp"
#include "commrat/mailbox/mailbox.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/platform/timestamp.hpp"

namespace commrat {

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
                    Milliseconds requested_period = Milliseconds::zero(),
                    Milliseconds poll_timeout = Milliseconds(100),
                    Milliseconds cmd_timeout = Milliseconds(1000))
        : CmdInput<Registry, OutputType>(work_mbx, producer_system_id, producer_instance_id, cmd_timeout)
        , data_mbx_(data_mbx)
        , requested_period_(requested_period)
        , poll_timeout_(poll_timeout)
        , subscribed_(false)
        , actual_period_(Milliseconds::zero())
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
    bool subscribe(Milliseconds& actual_period, Milliseconds timeout = Milliseconds::zero()) {
        // Build subscribe request
        TimsMessage<SubscribeRequestPayload> request{
            .header = {
                .msg_type = Registry::template get_message_id<SubscribeRequestPayload>(),
                .msg_size = 0,  // Will be set by serialization
                .timestamp = Time::now(),
                .seq_number = 0,
                .dest = 0,  // Will be set by subscribe()
                .src = data_mbx_.mailbox_id(),  // Producer sends data here
                .flags = 0
            },
            .payload = {
                .subscriber_addr = data_mbx_.mailbox_id(),
                .requested_period_ms = std::chrono::duration_cast<Milliseconds>(requested_period_).count()
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
    bool unsubscribe(Milliseconds timeout = Milliseconds::zero()) {
        if (!subscribed_) {
            return true;  // Already unsubscribed
        }
        
        // Build unsubscribe request
        TimsMessage<UnsubscribeRequestPayload> request{
            .header = {
                .msg_type = Registry::template get_message_id<UnsubscribeRequestPayload>(),
                .timestamp = Time::now(),
                .seq_number = 0,
                .src = data_mbx_.mailbox_id(),
            },
            .payload = {
                .subscriber_addr = data_mbx_.mailbox_id()
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
        if (!data_mbx_.receive(last_message_, poll_timeout_)) {
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
        return last_message_.header.sequence_number;
    }
    
    /**
     * @brief Get message ID from header (convenience)
     */
    uint32_t get_message_id() const {
        return last_message_.header.message_id;
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
     * @brief Legacy poll_data interface (copies message) - DEPRECATED
     * 
     * Use poll_data() + get_payload() for zero-copy instead.
     * 
     * @deprecated Use zero-copy API: poll_data() then get_payload()
     */
    bool poll_data(TimsMessage<OutputType>& msg) {
        if (!data_mbx_.receive(msg, poll_timeout_)) {
            return false;
        }
        // Also update internal storage for consistency
        last_message_ = msg;
        has_data_ = true;
        return true;
    }
    
    /**
     * @brief Check if subscribed to producer
     */
    bool is_subscribed() const { return subscribed_; }
    
    /**
     * @brief Get actual update period from producer
     */
    Milliseconds get_actual_period() const { return actual_period_; }
    
private:
    MailboxFor<Registry>& data_mbx_;       ///< Dedicated mailbox for data stream
    Milliseconds requested_period_;         ///< Requested update period
    Milliseconds poll_timeout_;             ///< Timeout for poll_data()
    bool subscribed_;                   ///< Currently subscribed?
    Milliseconds actual_period_;            ///< Actual period from producer
    TimsMessage<OutputType> last_message_{}; ///< Last received message (zero-copy storage)
    bool has_data_{false};              ///< True if last_message_ valid
};

} // namespace commrat
