/**
 * @file multi_input_processor.hpp
 * @brief Multi-input processing helpers for Module
 * 
 * Extracted from registry_module.hpp Phase 5.
 * Handles synchronized input gathering, getData synchronization,
 * and calling multi-input process functions.
 */

#pragma once

#include "commrat/mailbox/mailbox.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include <optional>
#include <tuple>

namespace commrat {

/**
 * @brief Multi-input processing mixin
 * 
 * Provides helpers for:
 * - Receiving from primary input mailbox
 * - Gathering all inputs synchronized to primary timestamp
 * - Calling multi-input process() methods with proper unpacking
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 * @tparam InputTypesTuple Tuple of input payload types
 * @tparam OutputData Single output type (or void)
 * @tparam OutputTypesTuple Tuple of output types (for multi-output)
 * @tparam InputCount Number of inputs
 */
template<typename ModuleType, typename InputTypesTuple, typename OutputData, typename OutputTypesTuple, std::size_t InputCount>
class MultiInputProcessor {
protected:
    /**
     * @brief Receive from primary input mailbox
     * 
     * Blocking receive that drives the execution rate of multi-input modules.
     * 
     * @tparam PrimaryIdx Index of primary input in InputTypesTuple
     * @return Received TimsMessage or error
     */
    template<std::size_t PrimaryIdx>
    auto receive_primary_input() -> MailboxResult<TimsMessage<std::tuple_element_t<PrimaryIdx, InputTypesTuple>>> {
        auto& module = static_cast<ModuleType&>(*this);
        
        if (!module.input_mailboxes_) {
            return MailboxResult<TimsMessage<std::tuple_element_t<PrimaryIdx, InputTypesTuple>>>(MailboxError::NotInitialized);
        }
        
        using PrimaryType = std::tuple_element_t<PrimaryIdx, InputTypesTuple>;
        auto& primary_mailbox = std::get<PrimaryIdx>(*module.input_mailboxes_);
        
        // BLOCKING receive - drives execution rate
        return primary_mailbox.template receive<PrimaryType>();
    }
    
    /**
     * @brief Gather all inputs synchronized to primary timestamp
     * 
     * Places primary input at its index, then uses getData to synchronize
     * all secondary inputs to the primary's timestamp.
     * 
     * @tparam PrimaryIdx Index of primary input
     * @tparam PrimaryMsgType Type of primary TimsMessage
     * @param primary_msg Received primary message with timestamp
     * @return Tuple of all inputs if sync succeeded, nullopt otherwise
     */
    template<std::size_t PrimaryIdx, typename PrimaryMsgType>
    std::optional<InputTypesTuple> gather_all_inputs(const PrimaryMsgType& primary_msg) {
        auto& module = static_cast<ModuleType&>(*this);
        
        if (!module.input_mailboxes_) {
            return std::nullopt;
        }
        
        // Create tuple to hold all inputs
        InputTypesTuple all_inputs{};
        
        // Place primary input at its index
        std::get<PrimaryIdx>(all_inputs) = primary_msg.payload;
        
        // Phase 6.10: Sync secondary inputs using getData with primary timestamp from header
        // TimsMessage.header.timestamp is the authoritative timestamp
        bool all_synced = sync_secondary_inputs<PrimaryIdx>(primary_msg.header.timestamp, all_inputs);
        
        if (!all_synced) {
            return std::nullopt;
        }
        
        return all_inputs;
    }
    
    /**
     * @brief Call multi-input process with single output
     * 
     * Unpacks InputTypesTuple and calls process(const T1&, const T2&, ...)
     * 
     * @param inputs Tuple of all input payloads
     * @return Single output data
     */
    OutputData call_multi_input_process(const InputTypesTuple& inputs) {
        return call_multi_input_process_impl(inputs, std::make_index_sequence<InputCount>{});
    }
    
    /**
     * @brief Call multi-input process with multi-output
     * 
     * Unpacks both tuples and calls process(const T1&, ..., O1&, O2&, ...)
     * 
     * @param inputs Tuple of all input payloads
     * @param outputs Tuple of output payloads (passed by reference)
     */
    void call_multi_input_multi_output_process(const InputTypesTuple& inputs, OutputTypesTuple& outputs) {
        call_multi_input_multi_output_process_impl(inputs, outputs, 
                                                    std::make_index_sequence<InputCount>{},
                                                    std::make_index_sequence<std::tuple_size_v<OutputTypesTuple>>{});
    }
    
private:
    /**
     * @brief Sync all secondary inputs via getData
     */
    template<std::size_t PrimaryIdx>
    bool sync_secondary_inputs(uint64_t primary_timestamp, InputTypesTuple& all_inputs) {
        return sync_secondary_inputs_impl<PrimaryIdx>(primary_timestamp, all_inputs, 
                                                       std::make_index_sequence<InputCount>{});
    }
    
    /**
     * @brief Sync secondary inputs implementation (fold expression over indices)
     */
    template<std::size_t PrimaryIdx, std::size_t... Is>
    bool sync_secondary_inputs_impl(uint64_t primary_timestamp, InputTypesTuple& all_inputs,
                                     std::index_sequence<Is...>) {
        // For each input index (except primary), call getData
        bool all_success = true;
        
        // Fold expression: process each secondary input
        ((Is != PrimaryIdx ? 
          (all_success = sync_input_at_index<Is>(primary_timestamp, all_inputs) && all_success) : 
          true), ...);
        
        return all_success;
    }
    
    /**
     * @brief Sync a single secondary input at given index
     * 
     * Uses getData with tolerance to find message closest to primary timestamp.
     * Updates input metadata on success, marks invalid on failure.
     */
    template<std::size_t Index>
    bool sync_input_at_index(uint64_t primary_timestamp, InputTypesTuple& all_inputs) {
        auto& module = static_cast<ModuleType&>(*this);
        using InputType = std::tuple_element_t<Index, InputTypesTuple>;
        auto& mailbox = std::get<Index>(*module.input_mailboxes_);
        
        // Non-blocking getData with tolerance
        auto result = mailbox.template getData<InputType>(
            primary_timestamp,
            module.config_.sync_tolerance,
            InterpolationMode::NEAREST
        );
        
        if (!result.has_value()) {
            // Phase 6.10: Mark input as invalid
            module.mark_input_invalid(Index);
            return false;  // getData failed
        }
        
        // Phase 6.10: Populate metadata for this input
        // Index matches position in Inputs<T1, T2, T3, ...>
        // getData succeeded - data is "new" (successfully retrieved from buffer)
        // Note: is_new_data = true means getData returned a value (not nullopt)
        //       is_new_data = false would indicate using fallback/default data
        module.update_input_metadata(Index, result.value(), true);
        
        // Store payload in tuple
        std::get<Index>(all_inputs) = result->payload;
        return true;
    }
    
    /**
     * @brief Call multi-input process implementation (single output)
     */
    template<std::size_t... Is>
    OutputData call_multi_input_process_impl(const InputTypesTuple& inputs, std::index_sequence<Is...>) {
        auto& module = static_cast<ModuleType&>(*this);
        
        // Unpack tuple and call process(const T1&, const T2&, ...)
        using Base = MultiInputProcessorBase<InputTypesTuple, OutputData, InputCount>;
        return static_cast<Base*>(&module)->process(std::get<Is>(inputs)...);
    }
    
    /**
     * @brief Call multi-input process implementation (multi-output)
     */
    template<std::size_t... InputIs, std::size_t... OutputIs>
    void call_multi_input_multi_output_process_impl(const InputTypesTuple& inputs, OutputTypesTuple& outputs,
                                                      std::index_sequence<InputIs...>,
                                                      std::index_sequence<OutputIs...>) {
        auto& module = static_cast<ModuleType&>(*this);
        
        // Unpack both tuples and call process(const T1&, ..., O1&, O2&, ...)
        using Base = MultiInputProcessorBase<InputTypesTuple, OutputTypesTuple, InputCount>;
        static_cast<Base*>(&module)->process(std::get<InputIs>(inputs)..., std::get<OutputIs>(outputs)...);
    }
};

} // namespace commrat
