/**
 * @file loop_executor.hpp
 * @brief Data processing loop implementations for different input modes
 * 
 * Provides loop functions for:
 * - PeriodicInput: periodic_loop (time-driven)
 * - LoopInput: free_loop (maximum throughput)
 * - Input<T>: continuous_loop (event-driven, single input)
 * - Inputs<T, U, V>: multi_input_loop (synchronized multi-input)
 * 
 * Phase 6.10: All loops use TimsMessage.header.timestamp as single source of truth
 */

#pragma once

#include <commrat/platform/timestamp.hpp>
#include <iostream>
#include <thread>
#include <atomic>

namespace commrat {

/**
 * @brief Loop execution logic for Module data threads
 * 
 * This class provides loop implementations as a mixin base class.
 * Module inherits from LoopExecutor to get loop methods.
 * 
 * Template parameters match Module's parameters for access to:
 * - config_, running_
 * - process(), process_continuous()
 * - Publishing methods
 * - Multi-input helper methods
 */
template<
    typename ModuleType  // CRTP: Derived Module type
>
class LoopExecutor {
protected:
    ModuleType& module() { return static_cast<ModuleType&>(*this); }
    const ModuleType& module() const { return static_cast<const ModuleType&>(*this); }
    
public:
    /**
     * @brief Periodic loop - time-driven data generation
     * 
     * Generates output at fixed intervals (config_.period).
     * Phase 6.10: Captures timestamp at generation moment.
     * 
     * Used for: PeriodicInput modules
     */
    void periodic_loop() {
        auto& mod = module();
        std::cout << "[" << mod.config_.name << "] periodic_loop started, period=" 
                  << mod.config_.period.count() << "ms\n";
        
        uint32_t iteration = 0;
        while (mod.running_) {
            if (iteration < 3) {
                std::cout << "[" << mod.config_.name << "] periodic_loop iteration " << iteration << "\n";
            }
            
            // Phase 6.10: Capture timestamp at data generation moment
            uint64_t generation_timestamp = Time::now();
            
            if constexpr (ModuleType::has_multi_output) {
                // Multi-output: create tuple and call process with references
                typename ModuleType::OutputTypesTuple outputs{};
                // Unpack tuple and call multi-output process(Ts&...) via virtual dispatch
                // Must use MultiOutputProcessorBase explicitly to avoid ambiguity with SingleOutputProcessorBase
                using MultiOutBase = MultiOutputProcessorBase<
                    typename ModuleType::OutputTypesTuple,
                    typename ModuleType::InputData
                >;
                std::apply([&mod](auto&... args) { 
                    static_cast<MultiOutBase&>(mod).process(args...);
                }, outputs);
                // Phase 6.10: Publish with automatic header.timestamp
                mod.publish_multi_outputs_with_timestamp(outputs, generation_timestamp);
            } else {
                // Single output: call process() with virtual dispatch
                auto output = mod.process();  // Virtual call to derived class
                // Phase 6.10: Wrap in TimsMessage with header.timestamp = generation time
                auto tims_msg = mod.create_tims_message(std::move(output), generation_timestamp);
                mod.publish_tims_message(tims_msg);
            }
            
            std::this_thread::sleep_for(mod.config_.period);
            iteration++;
        }
        
        std::cout << "[" << mod.config_.name << "] periodic_loop ended after " << iteration << " iterations\n";
    }
    
    /**
     * @brief Free loop - maximum throughput data generation
     * 
     * Generates output as fast as possible without delays.
     * Phase 6.10: Captures timestamp at generation moment.
     * 
     * Used for: LoopInput modules
     */
    void free_loop() {
        auto& mod = module();
        
        while (mod.running_) {
            // Phase 6.10: Capture timestamp at data generation moment
            uint64_t generation_timestamp = Time::now();
            
            if constexpr (ModuleType::has_multi_output) {
                // Multi-output: create tuple and call process with references
                typename ModuleType::OutputTypesTuple outputs{};
                // Must use MultiOutputProcessorBase explicitly to avoid ambiguity
                using MultiOutBase = MultiOutputProcessorBase<
                    typename ModuleType::OutputTypesTuple,
                    typename ModuleType::InputData
                >;
                std::apply([&mod](auto&... args) { 
                    static_cast<MultiOutBase&>(mod).process(args...);
                }, outputs);
                mod.publish_multi_outputs_with_timestamp(outputs, generation_timestamp);
            } else {
                // Single output: call process() with virtual dispatch
                auto output = mod.process();
                auto tims_msg = mod.create_tims_message(std::move(output), generation_timestamp);
                mod.publish_tims_message(tims_msg);
            }
        }
    }
    
    /**
     * @brief Continuous loop - event-driven single input processing
     * 
     * Blocks on data mailbox, processes each received message.
     * Phase 6.10: Uses input timestamp from header (data validity time).
     * 
     * Used for: Input<T> modules (single continuous input)
     */
    void continuous_loop() {
        auto& mod = module();
        std::cout << "[" << mod.config_.name << "] continuous_loop started, waiting for data...\n";
        
        while (mod.running_) {
            // BLOCKING receive on data mailbox - no timeout, waits for data
            auto result = mod.data_mailbox_->template receive<typename ModuleType::InputData>();
            
            if (result) {
                // Phase 6.10: Populate metadata BEFORE process call
                // Single continuous input always uses index 0
                mod.update_input_metadata(0, result.value(), true);  // Always new data for continuous
                
                auto output = mod.process_continuous_dispatch(result->payload);
                // Phase 6.10: Use input timestamp from header (data validity time)
                auto tims_msg = mod.create_tims_message(std::move(output), result->header.timestamp);
                mod.publish_tims_message(tims_msg);
            }
        }
        
        std::cout << "[" << mod.config_.name << "] continuous_loop ended\n";
    }
    
    /**
     * @brief Multi-input loop - synchronized multi-input processing
     * 
     * Blocks on primary input, synchronizes secondary inputs via getData(),
     * processes all inputs together.
     * Phase 6.10: Uses primary input timestamp as synchronization point.
     * 
     * Used for: Inputs<T, U, V> modules with PrimaryInput<T> specified
     */
    void multi_input_loop() {
        auto& mod = module();
        static_assert(ModuleType::has_multi_input, "multi_input_loop only for multi-input modules");
        
        std::cout << "[" << mod.config_.name << "] multi_input_loop started (" 
                  << ModuleType::InputCount << " inputs)\n";
        
        // Identify primary input index
        constexpr size_t primary_idx = ModuleType::get_primary_input_index();
        std::cout << "[" << mod.config_.name << "] Primary input index: " << primary_idx << "\n";
        
        uint32_t loop_iteration = 0;
        while (mod.running_) {
            // Step 1: BLOCK on primary input (drives execution)
            if (loop_iteration < 3) {
                std::cout << "[" << mod.config_.name << "] Waiting for primary input... (iteration " 
                          << loop_iteration << ")\n";
            }
            
            auto primary_result = mod.template receive_primary_input<primary_idx>();
            
            if (!primary_result.has_value()) {
                if (loop_iteration < 3) {
                    std::cout << "[" << mod.config_.name << "] No primary data received\n";
                }
                loop_iteration++;
                continue;
            }
            
            if (loop_iteration < 3) {
                std::cout << "[" << mod.config_.name << "] Primary input received!\n";
            }
            
            // Phase 6.10: Populate primary metadata
            mod.update_input_metadata(0, primary_result.value(), true);
            
            // Step 2: Sync all secondary inputs
            auto all_inputs = mod.template gather_all_inputs<primary_idx>(primary_result.value());
            
            if (!all_inputs) {
                if (loop_iteration < 3) {
                    std::cout << "[" << mod.config_.name << "] Failed to sync inputs\n";
                }
                loop_iteration++;
                continue;
            }
            
            if (loop_iteration < 3) {
                std::cout << "[" << mod.config_.name << "] All inputs synced, calling process()\n";
            }
            
            // Phase 6.10: Extract primary timestamp (synchronization point)
            uint64_t primary_timestamp = primary_result->header.timestamp;
            
            // Step 3: Call process with all inputs
            if constexpr (ModuleType::has_multi_output) {
                typename ModuleType::OutputTypesTuple outputs{};
                mod.call_multi_input_multi_output_process(*all_inputs, outputs);
                mod.publish_multi_outputs_with_timestamp(outputs, primary_timestamp);
            } else {
                auto output = mod.call_multi_input_process(*all_inputs);
                auto tims_msg = mod.create_tims_message(std::move(output), primary_timestamp);
                mod.publish_tims_message(tims_msg);
            }
            
            loop_iteration++;
        }
        
        std::cout << "[" << mod.config_.name << "] multi_input_loop ended\n";
    }
};

} // namespace commrat
