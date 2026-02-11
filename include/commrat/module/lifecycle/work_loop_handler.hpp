#pragma once

#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include <iostream>
#include <type_traits>

namespace commrat {

/**
 * @brief Phase 7: Work Loop Handler CRTP Mixin
 * 
 * Handles subscription protocol messages on the WORK mailbox:
 * - SubscribeRequest: Producer receives subscription from consumer
 * - SubscribeReply: Consumer receives acknowledgment from producer
 * - UnsubscribeRequest: Producer receives unsubscription from consumer
 * 
 * This is the main dispatch loop for the subscription protocol.
 * Runs in a dedicated thread spawned by LifecycleManager.
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 */
template<typename ModuleType>
class WorkLoopHandler {
protected:
    /**
     * @brief Main work loop - handles subscription protocol messages
     * 
     * Runs continuously while module is running, processing subscription
     * messages on the WORK mailbox. Uses blocking receive (no polling).
     * 
     * Message types handled:
     * - SubscribeRequest: Add subscriber to output list
     * - SubscribeReply: Confirm subscription established
     * - UnsubscribeRequest: Remove subscriber from output list
     */
    void work_loop() {
        auto& module = static_cast<ModuleType&>(*this);
        
        // Just print a simple message - the address details aren't critical for the log
        std::cout << "[" << module.config_.name << "] work_loop started on WORK mailbox\n" << std::flush;
        
        while (module.running_) {
            // Use receive_any with visitor pattern on work_mailbox
            // BLOCKING receive - waits indefinitely for subscription messages
            std::cout << "[" << module.config_.name << "] work_loop: waiting for message...\n" << std::flush;
            
            auto visitor = [&module](auto&& tims_msg) {
                // tims_msg is TimsMessage<PayloadT>, extract payload
                auto& msg = tims_msg.payload;
                using MsgType = std::decay_t<decltype(msg)>;
                
                // Handle subscription protocol
                if constexpr (std::is_same_v<MsgType, SubscribeRequestType>) {
                    std::cout << "[" << module.config_.name << "] Handling SubscribeRequest\n";
                    module.handle_subscribe_request(msg);
                } else if constexpr (std::is_same_v<MsgType, SubscribeReplyType>) {
                    std::cout << "[" << module.config_.name << "] Handling SubscribeReply\n";
                    module.handle_subscribe_reply(msg);
                } else if constexpr (std::is_same_v<MsgType, UnsubscribeRequestType>) {
                    std::cout << "[" << module.config_.name << "] Handling UnsubscribeRequest\n";
                    module.handle_unsubscribe_request(msg);
                }
            };
            
            // BLOCKING receive on work mailbox (no timeout)
            module.work_mailbox().receive_any(visitor);
        }
        
        std::cout << "[" << module.config_.name << "] work_loop ended\n";
    }
};

}  // namespace commrat
