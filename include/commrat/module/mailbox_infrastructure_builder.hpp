#pragma once

#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/module/mailbox_set.hpp"
#include "commrat/module/module_config.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include <tuple>
#include <utility>

namespace commrat {

/**
 * @brief Phase 8: Mailbox Infrastructure Builder
 * 
 * Static factory methods for creating mailbox infrastructure:
 * - Single-output: Creates individual CMD, WORK, PUBLISH mailboxes
 * - Multi-output: Creates tuple of MailboxSets (one per output type)
 * 
 * This is pure compile-time dispatch - no instance state.
 * 
 * @tparam ModuleType The derived Module class (for type access only)
 * @tparam UserRegistry The message registry type
 */
template<typename ModuleType, typename UserRegistry>
class MailboxInfrastructureBuilder {
protected:
    /**
     * @brief Create mailbox infrastructure (Phase 7.4)
     * 
     * Single output: Returns tuple<CmdMailbox, WorkMailbox, PublishMailbox>
     * Multi-output: Returns tuple<MailboxSet<T1>, MailboxSet<T2>, ...>
     * 
     * @param config Module configuration with system_id, instance_id, etc.
     * @return Mailbox infrastructure (tuple of mailboxes or MailboxSets)
     */
    static auto create_mailbox_infrastructure(const ModuleConfig& config) {
        if constexpr (!ModuleType::use_mailbox_sets) {
            // Single output: Create traditional mailboxes
            typename ModuleType::CmdMailbox cmd(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<typename ModuleType::OutputData, typename ModuleType::OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::CMD),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_cmd"
            });
            
            typename ModuleType::WorkMailbox work(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<typename ModuleType::OutputData, typename ModuleType::OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::WORK),
                .message_slots = config.message_slots,
                .max_message_size = SystemRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_work"
            });
            
            typename ModuleType::PublishMailbox publish(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<typename ModuleType::OutputData, typename ModuleType::OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::PUBLISH),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_publish"
            });
            
            return std::make_tuple(std::move(cmd), std::move(work), std::move(publish));
        } else {
            // Multi-output: Create tuple of MailboxSets
            return create_mailbox_sets_impl(config, std::make_index_sequence<ModuleType::num_output_types>{});
        }
    }
    
    /**
     * @brief Helper to create MailboxSet tuple for multi-output modules
     * 
     * Uses fold expression to construct tuple with initialized MailboxSets,
     * one for each output type.
     * 
     * @tparam Is Index sequence (0, 1, 2, ..., num_output_types-1)
     * @param config Module configuration
     * @return Tuple of MailboxSets
     */
    template<std::size_t... Is>
    static auto create_mailbox_sets_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        using MailboxSetTuple = typename ModuleType::MailboxSetTuple;
        // Use fold expression to construct tuple with initialized MailboxSets
        return MailboxSetTuple{create_mailbox_set<Is>(config)...};
    }
    
    /**
     * @brief Create a single MailboxSet for output type at index I
     * 
     * Each output type gets its own MailboxSet with CMD, WORK, PUBLISH mailboxes
     * at the appropriate base address for that output type.
     * 
     * @tparam I Output type index
     * @param config Module configuration
     * @return Initialized MailboxSet for output type I
     */
    template<std::size_t I>
    static auto create_mailbox_set(const ModuleConfig& config) {
        using OutputTypesTuple = typename ModuleType::OutputTypesTuple;
        using OutputType = std::tuple_element_t<I, OutputTypesTuple>;
        
        // MailboxSet template parameters: Registry, OutputType
        MailboxSet<UserRegistry, OutputType> set;
        set.initialize(config);
        return set;
    }
};

}  // namespace commrat
