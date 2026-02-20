#pragma once

#include "cmd_input.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include <optional>

/**
 * @brief Continuous input interface - receives data stream
 * 
 * Receives data via push model from producer. Data arrives asynchronously
 * and is consumed immediately in process() without local buffering.
 * 
 * Flow:
 * 1. Subscribe to producer via work_mbx (sends SubscribeRequest)
 * 2. Producer acknowledges via work_mbx (receives SubscriptionAck)
 * 3. Producer publishes data to our data_mbx (ongoing stream)
 * 4. poll_data() receives from data_mbx and passes to process()
 * 
 * NO local buffer - data goes directly to process()
 * Buffers exist only on producer side (ModuleOutput)
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 * This abstracts RACK's push proxy for real-time data streaming
 */
template<typename CommratApp, typename T>
class ContinuousInput : public CmdInput<CommratApp, T>
{
public:
    using Type = T;
    using ConfigType = ContinuousInputConfig;
    using WorkMailbox = TypedMailbox<typename CommratApp::SystemRegistry::SubscriptionAck>;
    
    /**
     * @brief Construct continuous input
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     * @param work_mbx Reference to module's shared work mailbox
     * @param poll_timeout How long to wait for new data in poll_data()
     */
    ContinuousInput(SystemId producer_system_id, 
                    InstanceId producer_instance_id,
                    WorkMailbox& work_mbx,
                    Duration poll_timeout = Milliseconds(100))
        : CmdInput<CommratApp, T>(producer_system_id, producer_instance_id)
        , work_mbx_(work_mbx)
        , poll_timeout_(poll_timeout)
        , data_mbx_()  // TODO: Calculate data mailbox address
    {}
    
    /**
     * @brief Subscribe to producer's output
     * Sends SubscribeRequest to producer's command mailbox
     * @return Success or error
     */
    auto subscribe() -> MailboxResult<void> {
        send_command(SubscribeRequest{});
        // TODO: Send SubscribeRequest via work_mbx_ to this->producer_cmd_address_
        return MailboxResult<void>::success();
    }
    
    /**
     * @brief Unsubscribe from producer's output
     * @return Success or error
     */
    auto unsubscribe() -> MailboxResult<void> {
        // TODO: Send UnsubscribeRequest via work_mbx_ to this->producer_cmd_address_
        return MailboxResult<void>::success();
    }
    
    /**
     * @brief Poll for new data from producer
     * 
     * Blocking receive with timeout. Called by framework to fetch data
     * for process() execution.
     * 
     * @return Received message or error (timeout, etc.)
     */
    auto poll_data() -> MailboxResult<TimsMessage<T>> {
        return data_mbx_.receive(poll_timeout_);
    }
    
private:
    WorkMailbox& work_mbx_;           // Reference to module's shared work mailbox
    Duration poll_timeout_;            // How long to block waiting for data
    TypedMailbox<T> data_mbx_;        // Receives data stream from producer
};
