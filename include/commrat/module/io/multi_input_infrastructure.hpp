/**
 * @file multi_input_infrastructure.hpp
 * @brief Multi-input infrastructure using new Input classes
 * 
 * Simplified - uses ContinuousInput (primary) + SyncedInput (secondaries)
 * No more HistoricalMailbox tuple - each input is strongly typed
 */

#pragma once

#include "commrat/module/io/input/continuous_input.hpp"
#include "commrat/module/io/input/synced_input.hpp"
#include "commrat/module/io/config/input_config.hpp"
#include <tuple>
#include <optional>

namespace commrat {

/**
 * @brief Multi-input infrastructure mixin
 * 
 * Creates tuple of inputs: First is ContinuousInput (primary), rest are SyncedInput (secondary)
 * Primary drives execution, secondaries fetch data via get_data(primary_timestamp)
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 * @tparam CommratApp Application type (provides message registry)
 * @tparam InputTypes Tuple of input payload types (first is primary)
 */
template<typename ModuleType, typename CommratApp, typename InputTypes>
class MultiInputInfrastructure {
protected:
    // Extract types from tuple
    static constexpr std::size_t InputCount = std::tuple_size_v<InputTypes>;
    
    // Primary input is first type (ContinuousInput)
    using PrimaryType = std::tuple_element_t<0, InputTypes>;
    
    // Helper to create input tuple type
    // First is ContinuousInput<T0>, rest are SyncedInputImpl<T1>, SyncedInputImpl<T2>, ...
    template<std::size_t... Is>
    static auto make_input_tuple_type(std::index_sequence<Is...>) {
        using First = ContinuousInput<CommratApp, PrimaryType>;
        
        if constexpr (InputCount == 1) {
            return std::tuple<First>{};
        } else {
            return std::tuple<
                First,
                SyncedInputImpl<CommratApp, std::tuple_element_t<Is + 1, InputTypes>>...
            >{};
        }
    }
    
    using InputTuple = decltype(make_input_tuple_type(
        std::make_index_sequence<InputCount - 1>{}
    ));
    
    std::optional<InputTuple> inputs_;
    
    /**
     * @brief Initialize multi-input instances
     * 
     * Creates ContinuousInput for primary + SyncedInput for each secondary
     * TODO: Extract addresses from config and construct input tuple
     */
    void initialize_inputs() {
        // TODO: Implement
        // Primary: ContinuousInput with subscribe()
        // Secondaries: SyncedInput with get_data()
    }
    
    /**
     * @brief Get primary input (always index 0)
     */
    auto& get_primary_input() {
        return std::get<0>(*inputs_);
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
