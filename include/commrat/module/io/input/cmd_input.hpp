#pragma once

#include "commrat/commrat.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"

/**
 * @brief Command-only input interface
 * 
 * Provides command interface to a remote module without receiving data.
 * Useful for sending control commands (SetRate, Reset, etc.) to modules
 * without subscribing to their output data stream.
 * 
 * Does NOT participate in process() - only provides command methods.
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 */
template<typename CommratApp, typename T>
class CmdInput 
    requires (is_commrat_message_v<T>),
             (CommratApp::UserRegistry::template is_registered<T>),
             (is_message_registry_v<typename CommratApp::UserRegistry>) 
{
public:
    using Type = T;
    using ConfigType = CmdInputConfig;
    using CommandMailbox = TypedMailbox<typename CommratApp::SystemRegistry::SystemCommands, 
                                        typename CommratApp::UserRegistry::template UserCommands<T>>;
    
    /**
     * @brief Construct command input
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     */
    CmdInput(SystemId producer_system_id, InstanceId producer_instance_id)
        : producer_system_id_(producer_system_id)
        , producer_instance_id_(producer_instance_id)
        , producer_cmd_address_(calculate_cmd_mailbox_address(producer_system_id, 
                                                               producer_instance_id, 
                                                               CommratApp::template get_message_id<T>()))
    {}
    
    /**
     * @brief Send command to producer module
     * @tparam CmdType Command type (must be valid for this output type)
     * @param command Command to send
     * @return Result of send operation
     */
    template<typename CmdType>
    CmdType::ReplyType send_command(const CmdType& command) -> CmdType::ReplyType {
        // TODO: Implement using TiMS send to producer_cmd_address_
        return CmdType::ReplyType{}; // MailboxResult<void>::success();
    }
    
protected:
    SystemId producer_system_id_;
    InstanceId producer_instance_id_;
    uint32_t producer_cmd_address_;  // Producer's command mailbox address
};
