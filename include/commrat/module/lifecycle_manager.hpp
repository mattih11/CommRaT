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
        
        // Phase 7.4: Start mailboxes (all MailboxSets for multi-output)
        if constexpr (module.use_mailbox_sets) {
            // Multi-output: start all MailboxSets
            module.template start_all_mailbox_sets(std::make_index_sequence<module.num_output_types>{});
        } else {
            // Single-output: start individual mailboxes
            auto cmd_result = module.cmd_mailbox().start();
            if (!cmd_result) {
                throw std::runtime_error("Failed to start command mailbox");
            }
            
            auto work_result = module.work_mailbox().start();
            if (!work_result) {
                throw std::runtime_error("Failed to start work mailbox");
            }
            
            auto publish_result = module.publish_mailbox().start();
            if (!publish_result) {
                throw std::runtime_error("Failed to start publish mailbox");
            }
        }
        
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
        if constexpr (module.use_mailbox_sets) {
            // Multi-output: spawn one thread per MailboxSet
            std::cout << "[" << module.config_.name << "] Spawning " << module.num_output_types << " output work threads...\n";
            module.template spawn_all_output_work_threads(std::make_index_sequence<module.num_output_types>{});
        } else {
            // Single-output: one work thread
            module.work_thread_ = std::thread(&ModuleType::work_loop, &module);
        }
        
        // Start command thread for user commands
        module.command_thread_ = std::thread(&ModuleType::command_loop, &module);
        
        // Give threads time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Subscribe to source(s)
        if constexpr (module.has_multi_input) {
            // Phase 6.6: Multi-input subscription
            module.subscribe_to_all_sources();
        } else if constexpr (module.has_continuous_input) {
            // Single-input (backward compatible)
            if (module.config_.source_system_id && module.config_.source_instance_id) {
                module.subscribe_to_source(*module.config_.source_system_id, *module.config_.source_instance_id);
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
            for (const auto& source : module.config_.input_sources) {
                module.unsubscribe_from_multi_input_source(source);
            }
        } else if constexpr (module.has_continuous_input) {
            // Single continuous input (legacy)
            if (module.config_.source_system_id && module.config_.source_instance_id) {
                module.unsubscribe_from_source(*module.config_.source_system_id, *module.config_.source_instance_id);
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
        
        // Join work threads (single or multi-output)
        if constexpr (module.use_mailbox_sets) {
            // Multi-output: join all output work threads (via MultiOutputManager)
            module.join_output_work_threads();
        } else {
            // Single-output: join single work thread
            if (module.work_thread_ && module.work_thread_->joinable()) {
                module.work_thread_->join();
            }
        }
        
        if (module.command_thread_ && module.command_thread_->joinable()) {
            module.command_thread_->join();
        }
        
        // Stop all mailboxes
        if constexpr (module.use_mailbox_sets) {
            // Multi-output: stop all MailboxSets
            module.template stop_all_mailbox_sets(std::make_index_sequence<module.num_output_types>{});
        } else {
            // Single-output
            module.cmd_mailbox().stop();
            module.work_mailbox().stop();
        }
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
