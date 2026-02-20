#include <sertial/containers/ring_buffer.hpp>
#include "commrat/commrat.hpp"

/**
 * @brief Output interface for a module
 * 
 * Each output consists of:
 * - RingBuffer: Stores recent outputs for publishing to subscribers
 * - CommandMailbox: Receives system commands (subscribe/unsubscribe) and
 *   user-defined commands specific to this output type
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 */
template<typename CommratApp, typename T, std::size_t SLOTS = 10>
class ModuleOutput requires (is_commrat_message_v<T>),
                         (UserRegistry::template is_registered<T>),
                         is_message_registry_v<UserRegistry> {
public:
    using Type = T;
    // Command mailbox handles BOTH system commands AND user-defined commands for this output type
    using Commands = std::tuple<CommratApp::SystemRegistry::CommandTypes...,
                        CommratApp::template UserRegistry::CommandTypes<T>...>;
private:
    sertial::RingBuffer<Type, SLOTS> buffer_;       // Data storage (output-side only)
    TypedMailbox<Commands> cmd_mailbox_;            // Communication channel
    // cmd_mailbox receives: SubscribeRequest, UnsubscribeRequest, user commands
    // cmd_mailbox sends: SubscriptionAck to subscriber's work_mbx, data to subscriber's data_mbx
};