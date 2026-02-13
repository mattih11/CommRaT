/**
 * @file multi_input_infrastructure.hpp
 * @brief Multi-input mailbox infrastructure for Module
 * 
 * Extracted from registry_module.hpp Phase 4.
 * Handles creation, initialization, and management of HistoricalMailbox
 * instances for multi-input modules.
 */

#pragma once

#include "commrat/mailbox/historical_mailbox.hpp"
#include "commrat/module/module_config.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include <iostream>
#include <tuple>
#include <optional>
#include <thread>
#include <vector>

namespace commrat {

/**
 * @brief Multi-input infrastructure mixin
 * 
 * Provides mailbox creation, startup, and secondary input receive loops
 * for modules that consume multiple input types.
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 * @tparam UserRegistry Message registry
 * @tparam InputTypesTuple Tuple of input payload types
 * @tparam InputCount Number of inputs
 */
template<typename ModuleType, typename UserRegistry, typename InputTypesTuple, std::size_t InputCount>
class MultiInputInfrastructure {
protected:
    // Helper: Create HistoricalMailbox type for each input type
    template<typename T>
    using HistoricalMailboxFor = HistoricalMailbox<UserRegistry, 100>; // TODO: Make history size configurable
    
    // Generate tuple of HistoricalMailbox types from InputTypesTuple
    template<typename Tuple>
    struct MakeHistoricalMailboxTuple;
    
    template<typename... Ts>
    struct MakeHistoricalMailboxTuple<std::tuple<Ts...>> {
        using type = std::tuple<HistoricalMailboxFor<Ts>...>;
    };
    
    using HistoricalMailboxTuple = typename MakeHistoricalMailboxTuple<InputTypesTuple>::type;
    std::optional<HistoricalMailboxTuple> input_mailboxes_;
    
    std::vector<std::thread> secondary_input_threads_;
    
    /**
     * @brief Initialize multi-input mailboxes
     * 
     * Creates one HistoricalMailbox per input type at the correct address.
     */
    void initialize_multi_input_mailboxes() {
        create_input_mailboxes_impl(std::make_index_sequence<InputCount>{});
    }
    
    /**
     * @brief Start all input mailboxes
     * 
     * Calls start() on each HistoricalMailbox and checks for errors.
     */
    void start_input_mailboxes() {
        start_input_mailboxes_impl(std::make_index_sequence<InputCount>{});
    }
    
    /**
     * @brief Start receive threads for secondary inputs
     * 
     * Spawns background threads to continuously receive from all inputs
     * except the primary (which is handled by multi_input_loop).
     * 
     * @tparam PrimaryIdx Index of primary input
     */
    template<std::size_t PrimaryIdx>
    void start_secondary_input_threads() {
        start_secondary_threads_impl<PrimaryIdx>(std::make_index_sequence<InputCount>{});
    }
    
    /**
     * @brief Join all secondary input threads
     */
    void join_secondary_input_threads() {
        for (auto& thread : secondary_input_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
private:
    /**
     * @brief Create HistoricalMailbox for specific input at compile-time index
     */
    template<std::size_t Index>
    auto create_historical_mailbox_for_input() {
        auto& module = static_cast<ModuleType&>(*this);
        using InputType = std::tuple_element_t<Index, InputTypesTuple>;
        
        // Create DATA mailbox config for this input
        // Use input type's message ID to calculate base address
        constexpr uint32_t input_msg_id = UserRegistry::template get_message_id<InputType>();
        constexpr uint16_t input_type_id_low = static_cast<uint16_t>(input_msg_id & 0xFFFF);
        // Multi-input always uses non-indexed system_id/instance_id (subscriber addressing)
        uint32_t base_addr = (static_cast<uint32_t>(input_type_id_low) << 16) | 
                             (module.config_.system_id() << 8) | module.config_.instance_id();
        uint32_t data_mailbox_id = base_addr + static_cast<uint8_t>(MailboxType::DATA);
        
        std::cout << "[" << module.config_.name << "] Creating input mailbox[" << Index 
                  << "] at address " << data_mailbox_id << " (base=" << base_addr << ")\n";
        
        MailboxConfig mbx_config{
            .mailbox_id = data_mailbox_id,
            .message_slots = module.config_.message_slots,
            .max_message_size = UserRegistry::max_message_size,
            .send_priority = static_cast<uint8_t>(module.config_.priority),
            .realtime = module.config_.realtime,
            .mailbox_name = module.config_.name + "_data_" + std::to_string(Index)
        };
        
        return HistoricalMailboxFor<InputType>(
            mbx_config,
            module.config_.sync_tolerance()
        );
    }
    
    /**
     * @brief Create tuple of HistoricalMailbox instances
     */
    template<std::size_t... Is>
    void create_input_mailboxes_impl(std::index_sequence<Is...>) {
        input_mailboxes_ = std::make_tuple(
            create_historical_mailbox_for_input<Is>()...
        );
    }
    
    /**
     * @brief Start all input mailboxes (implementation)
     */
    template<std::size_t... Is>
    void start_input_mailboxes_impl(std::index_sequence<Is...>) {
        auto& module = static_cast<ModuleType&>(*this);
        
        if (input_mailboxes_) {
            // Check each mailbox start result
            ([&]() {
                auto result = std::get<Is>(*input_mailboxes_).start();
                if (!result) {
                    std::cerr << "[" << module.config_.name << "] ERROR: Failed to start input mailbox " 
                              << Is << " - error " << static_cast<int>(result.get_error()) << "\n";
                }
            }(), ...);
        }
    }
    
    /**
     * @brief Start threads for secondary inputs (all except primary)
     */
    template<std::size_t PrimaryIdx, std::size_t... Is>
    void start_secondary_threads_impl(std::index_sequence<Is...>) {
        auto& module = static_cast<ModuleType&>(*this);
        
        // Start thread for each input except primary
        ((Is != PrimaryIdx ? 
          (secondary_input_threads_.emplace_back(&ModuleType::template secondary_input_receive_loop<Is>, &module), true) : 
          true), ...);
    }
    
protected:
    /**
     * @brief Secondary input receive loop
     * 
     * Continuously receives from secondary input mailboxes to populate their
     * historical buffers. Called in background threads.
     */
    template<std::size_t InputIdx>
    void secondary_input_receive_loop() {
        auto& module = static_cast<ModuleType&>(*this);
        using InputType = std::tuple_element_t<InputIdx, InputTypesTuple>;
        auto& mailbox = std::get<InputIdx>(*input_mailboxes_);
        
        std::cout << "[" << module.config_.name << "] secondary_input_receive_loop[" << InputIdx << "] started\n";
        
        int receive_count = 0;
        while (module.running_) {
            // Blocking receive - stores in historical buffer automatically
            auto result = mailbox.template receive<InputType>();
            if (!result.has_value()) {
                std::cout << "[" << module.config_.name << "] secondary_input_receive_loop[" << InputIdx 
                          << "] receive failed after " << receive_count << " messages\n";
                break;
            }
            receive_count++;
            if (receive_count <= 3) {
                std::cout << "[" << module.config_.name << "] secondary_input_receive_loop[" << InputIdx 
                          << "] received message #" << receive_count 
                          << ", timestamp=" << result.value().header.timestamp << "\n";
            }
        }
        
        std::cout << "[" << module.config_.name << "] secondary_input_receive_loop[" << InputIdx 
                  << "] ended (total: " << receive_count << " messages)\n";
    }
};

} // namespace commrat
