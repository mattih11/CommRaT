#include <sertial/containers/ring_buffer.hpp>
#include "commrat/commrat.hpp"

/**
 * @brief Input interface for a module
 * 
 * Each input consists of:
 * - DataMailbox: Receives data from producer's output
 * - WorkMailbox reference: Shared module-level mailbox for subscription protocol
 * 
 * NO local buffer - inputs receive directly into process()
 * Buffers exist only on output side (producer stores data)
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 */
template<typename CommratApp, typename T>
class ModuleInput requires (is_commrat_message_v<T>),
                         (UserRegistry::template is_registered<T>),
                         is_message_registry_v<UserRegistry> {
public:
    using Type = T;
    using WorkMessages = std::tuple<CommratApp::SystemRegistry::SubscriptionCommands...>;
    
    // Subscription protocol
    ErrorType subscribe(...);      // Sends SubscribeRequest via work_mbx_
    ErrorType unsubscribe(...);    // Sends UnsubscribeRequest via work_mbx_
    
private:
    TypedMailbox<Type> data_mbx_;                  // Receives data from producer
    TypedMailbox<WorkMessages>& work_mbx_;         // Reference to module's shared work mailbox
    // work_mbx_ sends: SubscribeRequest to producer's cmd_mailbox
    // work_mbx_ receives: SubscriptionAck from producer's cmd_mailbox
    // Note: work_mbx_ is shared across ALL inputs in the module
};