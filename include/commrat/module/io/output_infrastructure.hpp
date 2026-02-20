/**
 * @file output_infrastructure.hpp
 * @brief Output infrastructure using BufferedOutput
 * 
 * Simplified - tuple of BufferedOutput instances, one per output type
 * Unified mechanism for single and multiple outputs (single is just tuple size 1)
 */

#pragma once

#include "commrat/module/io/output/buffered_output.hpp"
#include "commrat/module/io/config/output_config.hpp"
#include <tuple>
#include <optional>

namespace commrat {

/**
 * @brief Output infrastructure mixin
 * 
 * Creates tuple of BufferedOutput instances, one per output type
 * Unified mechanism: single output = tuple with one element, multiple = tuple with N elements
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 * @tparam CommratApp Application type (provides message registry)
 * @tparam OutputTypes Tuple of output payload types
 */
template<typename ModuleType, typename CommratApp, typename OutputTypes, std::size_t SLOTS = 100>
class OutputInfrastructure {
protected:
    static constexpr std::size_t OutputCount = std::tuple_size_v<OutputTypes>;
    
    // Helper to create output tuple type
    template<std::size_t... Is>
    static auto make_output_tuple_type(std::index_sequence<Is...>) {
        return std::tuple<
            BufferedOutput<CommratApp, std::tuple_element_t<Is, OutputTypes>, SLOTS>...
        >{};
    }
    
    using OutputTuple = decltype(make_output_tuple_type(
        std::make_index_sequence<OutputCount>{}
    ));
    
    std::optional<OutputTuple> outputs_;
    
    /**
     * @brief Initialize output instances
     * 
     * Creates BufferedOutput for each output type
     * TODO: Extract addresses from config
     */
    void initialize_outputs() {
        // TODO: Implement
        // Each output gets own BufferedOutput with unique address
    }
    
    /**
     * @brief Get output by index
     */
    template<std::size_t I>
        requires (I < OutputCount)
    auto& get_output() {
        return std::get<I>(*outputs_);
    }
    
    /**
     * @brief Publish to specific output
     */
    template<std::size_t I, typename T>
    auto publish(const T& data, Timestamp timestamp) {
        return get_output<I>().publish(data, timestamp);
    }
};

} // namespace commrat
