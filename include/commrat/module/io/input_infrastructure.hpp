/**
 * @file input_infrastructure.hpp
 * @brief Input infrastructure using new Input classes
 * 
 * Simplified - uses ContinuousInput (primary) + SyncedInput (secondaries)
 * No more HistoricalMailbox tuple - each input is strongly typed
 */

#pragma once

#include "commrat/module/io/input/continuous_input.hpp"
#include "commrat/module/io/input/synced_input.hpp"
#include "commrat/module/io/config/input_config.hpp"
#include "commrat/module/io/io_traits.hpp"
#include <tuple>
#include <optional>

namespace commrat {

/**
 * @brief Input infrastructure mixin
 * 
 * Creates tuple of inputs: First is ContinuousInput (primary), rest are SyncedInput (secondary)
 * Primary drives execution, secondaries fetch data via getData(primary_timestamp)
 * Unified mechanism for single and multiple inputs (single is just tuple size 1)
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 * @tparam CommratApp Application type (provides message registry)
 * @tparam InputTypes Tuple of input payload types (first is primary)
 */
template<typename ModuleType, typename CommratApp, typename InputTypes>
class InputInfrastructure {
    // Validate at compile time using concept (allows 0 or 1 ContinuousInput)
    static_assert(ValidInputTuple<typename InputInfrastructure::InputTuple>,
                  "Input tuple must contain at most one ContinuousInput");
                  
protected:
    // Extract types from tuple
    static constexpr std::size_t InputCount = std::tuple_size_v<InputTypes>;
    
    // Primary input is first type (ContinuousInput)
    using PrimaryType = std::tuple_element_t<0, InputTypes>;
    
    // Helper to create input tuple type
    // First is ContinuousInput<T0>, rest are SyncedInput<T1>, SyncedInput<T2>, ...
    template<std::size_t... Is>
    static auto make_input_tuple_type(std::index_sequence<Is...>) {
        using First = ContinuousInput<CommratApp, PrimaryType>;
        
        if constexpr (InputCount == 1) {
            return std::tuple<First>{};
        } else {
            return std::tuple<
                First,
                SyncedInput<CommratApp, std::tuple_element_t<Is + 1, InputTypes>>...
            >{};
        }
    }
    
    using InputTuple = decltype(make_input_tuple_type(
        std::make_index_sequence<InputCount - 1>{}
    ));
    
    std::optional<InputTuple> inputs_;
    
    // Find the ContinuousInput index at compile time (constexpr)
    static constexpr std::size_t PrimaryInputIndex = find_continuous_input_index_v<InputTuple>;
    
    /**
     * @brief Validate input tuple configuration at compile time
     */
    static consteval bool validate_input_tuple() {
        return has_at_most_one_continuous_input<InputTuple>();
    }
    
    /**
     * @brief Check if module has continuous input (input-driven execution)
     */
    static consteval bool has_continuous_input() {
        return has_single_continuous_input<InputTuple>();
    }
    
    /**
     * @brief Check if module needs periodic or free-running execution
     * (no ContinuousInput - relies on period config or loops continuously)
     */
    static consteval bool needs_periodic_or_loop_mode() {
        return has_no_continuous_input<InputTuple>();
    }
    
    /**
     * @brief Initialize input instances
     * 
     * Creates ContinuousInput for primary + SyncedInput for each secondary
     * Single input: Just ContinuousInput in tuple
     * Multiple inputs: ContinuousInput + SyncedInput(s)
     * TODO: Extract addresses from config and construct input tuple
     */
    void initialize_inputs() {
        // TODO: Implement
        // Primary: ContinuousInput with subscribe()
        // Secondaries: SyncedInput with getData()
    }
    
    /**
     * @brief Get primary input (the ContinuousInput)
     * 
     * Finds and returns the ContinuousInput from the tuple at compile time.
     * Not necessarily index 0 - uses io_traits to locate it.
     * Only available when module has a ContinuousInput.
     */
    auto& get_primary_input()
        requires (has_continuous_input())
    {
        return std::get<PrimaryInputIndex>(*inputs_);
    }
    
    const auto& get_primary_input() const
        requires (has_continuous_input())
    {
        return std::get<PrimaryInputIndex>(*inputs_);
    }
    
    /**
     * @brief Get input by index
     */
    template<std::size_t I>
        requires (I < InputCount)
    auto& get_input() {
        return std::get<I>(*inputs_);
    }
};

} // namespace commrat
