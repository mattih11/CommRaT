#pragma once

#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/module/mailbox/mailbox_set.hpp"
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
     * @brief Create mailbox infrastructure
     * 
     * Always returns tuple of MailboxSets (one per output type).
     * Single-output modules get a tuple with 1 MailboxSet.
     * 
     * @param config Module configuration with system_id, instance_id, etc.
     * @return Tuple of MailboxSets
     */
    static auto create_mailbox_infrastructure(const ModuleConfig& config) {
        // Always create tuple of MailboxSets
        return create_mailbox_sets_impl(config, std::make_index_sequence<ModuleType::num_output_types>{});
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
        // Create tuple with forwarding to ensure proper initialization
        return std::make_tuple(create_mailbox_set<Is>(config)...);
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
