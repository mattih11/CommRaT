#pragma once

#include <iostream>
#include <thread>
#include <chrono>

namespace commrat {

/**
 * @brief Phase 6: Lifecycle Management CRTP Mixin
 * 
 * Manages module startup and shutdown sequences:
 * - Mailbox initialization and startup
 * - Thread spawning (work, command, data, secondary inputs)
 * - Subscription setup
 * - Graceful shutdown and cleanup
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 */
template<typename ModuleType>
class LifecycleManager {
public:
    /**
     * @brief Start the module
     * 
     * Orchestrates the complete startup sequence:
     * 1. Call on_init() hook
     * 2. Start all mailboxes (CMD, WORK, PUBLISH, DATA, multi-input)
     * 3. Mark running_ = true
     * 4. Call on_start() hook
     * 5. Spawn work threads (subscription protocol handlers)
     * 6. Spawn command thread (user command handler)
     * 7. Subscribe to configured input sources
     * 8. Spawn data thread (periodic/loop/continuous/multi-input)
     */
    void start() {
        auto& module = static_cast<ModuleType&>(*this);
        
        if (module.running_) {
            return;
        }
        
        module.on_init();
        
        // Start mailboxes (always use MailboxSets, even for single output)
        module.template start_all_mailbox_sets(std::make_index_sequence<module.num_output_types>{});
        
        // Only start data mailbox for single ContinuousInput modules
        if (module.data_mailbox_) {
            auto data_result = module.data_mailbox_->start();
            if (!data_result) {
                throw std::runtime_error("Failed to start data mailbox");
            }
        }
        
        // Phase 6.6: Start multi-input mailboxes
        if constexpr (module.has_multi_input) {
            module.start_input_mailboxes();
        }
        
        module.running_ = true;
        module.on_start();
        
        // Start work thread(s) FIRST to handle subscriptions
        // Always use per-output work threads (even for single output)
        std::cout << "[" << module.config_.name << "] Spawning " << module.num_output_types << " output work threads...\n";
        module.template spawn_all_output_work_threads(std::make_index_sequence<module.num_output_types>{});
        
        // Start command thread for user commands (only if module has commands)
        if constexpr (module.num_command_types > 0) {
            module.command_thread_ = std::thread(&ModuleType::command_loop, &module);
        }
        
        // Give threads time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Subscribe to source(s)
        if constexpr (module.has_multi_input) {
            // Phase 6.6: Multi-input subscription
            module.subscribe_to_all_sources();
        } else if constexpr (module.has_continuous_input) {
            // Single-input (backward compatible)
            if (module.config_.has_single_input()) {
                module.subscribe_to_source(module.config_.source_system_id(), module.config_.source_instance_id());
            }
        }
        
        // Start data thread based on input mode
        if constexpr (module.has_periodic_input) {
            std::cout << "[" << module.config_.name << "] Starting periodic_loop thread...\n";
            module.data_thread_ = std::thread(&ModuleType::periodic_loop, &module);
        } else if constexpr (module.has_loop_input) {
            std::cout << "[" << module.config_.name << "] Starting free_loop thread...\n";
            module.data_thread_ = std::thread(&ModuleType::free_loop, &module);
        } else if constexpr (module.has_multi_input) {
            // Phase 6.6: Multi-input processing
            std::cout << "[" << module.config_.name << "] Starting multi_input_loop thread...\n";
            module.data_thread_ = std::thread(&ModuleType::multi_input_loop, &module);
            
            // Phase 6.9: Start secondary input receive threads
            // Primary input (index 0) is handled by multi_input_loop's blocking receive
            // Secondary inputs (indices 1, 2, ...) need background receive loops
            constexpr size_t primary_idx = ModuleType::get_primary_input_index();
            module.template start_secondary_input_threads<primary_idx>();
        } else if constexpr (module.has_continuous_input) {
            // Single continuous input (backward compatible)
            std::cout << "[" << module.config_.name << "] Starting continuous_loop thread...\n";
            module.data_thread_ = std::thread(&ModuleType::continuous_loop, &module);
        }
    }
    
    /**
     * @brief Stop the module
     * 
     * Orchestrates the complete shutdown sequence:
     * 1. Call on_stop() hook
     * 2. Unsubscribe from all input sources
     * 3. Mark running_ = false (signals threads to exit)
     * 4. Join data thread
     * 5. Join secondary input threads (multi-input)
     * 6. Join work threads (single or multi-output)
     * 7. Join command thread
     * 8. Stop all mailboxes
     * 9. Call on_cleanup() hook
     */
    void stop() {
        auto& module = static_cast<ModuleType&>(*this);
        
        if (!module.running_) {
            return;
        }
        
        module.on_stop();
        
        // Unsubscribe from source(s)
        if constexpr (module.has_multi_input) {
            // Multi-input: Unsubscribe from all configured sources
            for (const auto& source : module.config_.input_sources()) {
                module.unsubscribe_from_multi_input_source(source);
            }
        } else if constexpr (module.has_continuous_input) {
            // Single continuous input (legacy)
            if (module.config_.has_single_input()) {
                module.unsubscribe_from_source(module.config_.source_system_id(), module.config_.source_instance_id());
            }
        }
        
        module.running_ = false;
        
        // Wait for threads to finish
        if (module.data_thread_ && module.data_thread_->joinable()) {
            module.data_thread_->join();
        }
        
        // Phase 4: Join secondary input threads (via MultiInputInfrastructure)
        if constexpr (module.has_multi_input) {
            module.join_secondary_input_threads();
        }
        
        // Join work threads
        module.join_output_work_threads();
        
        if (module.command_thread_ && module.command_thread_->joinable()) {
            module.command_thread_->join();
        }
        
        // Stop all mailboxes
        module.template stop_all_mailbox_sets(std::make_index_sequence<module.num_output_types>{});
        if (module.data_mailbox_) {
            module.data_mailbox_->stop();
        }
        
        module.on_cleanup();
    }
    
    /**
     * @brief Check if module is currently running
     */
    bool is_running() const {
        const auto& module = static_cast<const ModuleType&>(*this);
        return module.running_;
    }
};

}  // namespace commrat
