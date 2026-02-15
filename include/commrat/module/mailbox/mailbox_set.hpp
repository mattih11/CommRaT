#pragma once

#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/module/module_config.hpp"
#include <optional>

namespace commrat {

/**
 * @brief Complete set of mailboxes for one output type
 * 
 * Each output type in a multi-output module gets its own MailboxSet.
 * All mailboxes share the same base address: [output_type_id:16][system_id:8][instance_id:8]
 * 
 * Architecture:
 * - CMD (base + 0):     Receives user commands for this output type
 * - WORK (base + 16):   Handles subscription requests for this output type
 * - PUBLISH (base + 32): Publishes this output type to subscribers
 * - DATA (base + 48):   Reserved for future use (inputs handled separately)
 * 
 * NOTE: These offsets are defined in MailboxType enum (module_config.hpp)
 *       All code MUST use MailboxType::DATA, not literal offsets!
 * 
 * @tparam UserRegistry The message registry for user-defined types
 * @tparam OutputType The specific output data type this set serves
 * @tparam CommandTypes Variadic list of command types (for CMD mailbox)
 */
template<
    typename UserRegistry,
    typename OutputType,
    typename... CommandTypes
>
struct MailboxSet {
    // CMD mailbox receives user commands and sends outputs
    using CmdMailbox = std::conditional_t<
        sizeof...(CommandTypes) == 0,
        // No user commands: send-only mailbox for outputs
        TypedMailbox<UserRegistry, SendOnlyTypes<OutputType>>,
        // User commands: receive commands, send outputs
        TypedMailbox<UserRegistry, ReceiveTypes<CommandTypes...>, SendOnlyTypes<OutputType>>
    >;
    
    using WorkMailbox = RegistryMailbox<SystemRegistry>;
    using PublishMailbox = TypedMailbox<UserRegistry, OutputType>;
    // DATA mailbox not needed here - inputs handled separately at module level
    
    std::optional<CmdMailbox> cmd;
    std::optional<WorkMailbox> work;
    std::optional<PublishMailbox> publish;
    
    uint32_t base_address{0};  // Cached for convenience
    
    /**
     * @brief Calculate base address for this output type
     */
    static constexpr uint32_t calculate_base(uint8_t system_id, uint8_t instance_id) {
        constexpr uint32_t type_id = UserRegistry::template get_message_id<OutputType>();
        constexpr uint16_t type_id_low = static_cast<uint16_t>(type_id & 0xFFFF);
        return (static_cast<uint32_t>(type_id_low) << 16) | (system_id << 8) | instance_id;
    }
    
    /**
     * @brief Initialize all mailboxes in this set
     * 
     * @tparam OutputIndex Index of this output in multi-output (default 0)
     */
    template<std::size_t OutputIndex = 0>
    void initialize(const ModuleConfig& config) {
        // For multi-output, use indexed access; for single/no output, use non-indexed
        uint8_t sys_id, inst_id;
        if (config.has_multi_output_config()) {
            sys_id = config.system_id(OutputIndex);
            inst_id = config.instance_id(OutputIndex);
        } else {
            sys_id = config.system_id();
            inst_id = config.instance_id();
        }
        base_address = calculate_base(sys_id, inst_id);
        
        // CMD mailbox
        cmd.emplace(MailboxConfig{
            .mailbox_id = base_address + static_cast<uint8_t>(MailboxType::CMD),
            .message_slots = config.message_slots,
            .max_message_size = UserRegistry::max_message_size,
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name + "_cmd_" + typeid(OutputType).name()
        });
        
        // WORK mailbox
        work.emplace(MailboxConfig{
            .mailbox_id = base_address + static_cast<uint8_t>(MailboxType::WORK),
            .message_slots = config.message_slots,
            .max_message_size = SystemRegistry::max_message_size,
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name + "_work_" + typeid(OutputType).name()
        });
        
        // PUBLISH mailbox
        publish.emplace(MailboxConfig{
            .mailbox_id = base_address + static_cast<uint8_t>(MailboxType::PUBLISH),
            .message_slots = config.message_slots,
            .max_message_size = UserRegistry::max_message_size,
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name + "_publish_" + typeid(OutputType).name()
        });
    }
};

} // namespace commrat
