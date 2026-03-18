#pragma once

#include "commrat/module/io/io_spec.hpp"
#include <tuple>
#include <type_traits>

namespace commrat {

// ============================================================================
// Output Type Extraction (from BuildIOTuple)
// ============================================================================

/**
 * @brief Extract output payload types from I/O tuple
 * 
 * Given BuildIOTuple result, extracts the actual payload types from ModuleOutput instances.
 * 
 * Example:
 *   IOTuple = std::tuple<ModuleOutput<Registry, SensorData>, ModuleOutput<Registry, DiagData>>
 *   → OutputTypes = std::tuple<SensorData, DiagData>
 */
template<typename IOBuilder, typename IOTuple>
struct ExtractOutputTypes {
private:
    template<size_t... Is>
    static constexpr auto extract_impl(std::index_sequence<Is...>) {
        return std::tuple<typename std::tuple_element_t<
            IOBuilder::output_indices()[Is], IOTuple
        >::Type...>{};
    }
    
public:
    using type = decltype(extract_impl(std::make_index_sequence<IOBuilder::num_outputs>{}));
};

template<typename IOBuilder, typename IOTuple>
using ExtractOutputTypes_t = typename ExtractOutputTypes<IOBuilder, IOTuple>::type;

/**
 * @brief Get single output type (when num_outputs == 1)
 */
template<typename IOBuilder, typename IOTuple, bool HasOutput = (IOBuilder::num_outputs == 1)>
struct SingleOutputType {
    using type = void;  // No output or multiple outputs
};

template<typename IOBuilder, typename IOTuple>
struct SingleOutputType<IOBuilder, IOTuple, true> {
    using type = std::tuple_element_t<0, ExtractOutputTypes_t<IOBuilder, IOTuple>>;
};

template<typename IOBuilder, typename IOTuple>
using SingleOutputType_t = typename SingleOutputType<IOBuilder, IOTuple>::type;

// ============================================================================
// Input Type Extraction (from BuildIOTuple)
// ============================================================================

/**
 * @brief Extract input payload types from I/O tuple
 * 
 * Given BuildIOTuple result, extracts the actual payload types from input instances.
 * 
 * Example:
 *   IOTuple = std::tuple<ModuleOutput<...>, ContinuousInput<Registry, SensorData>>
 *   → InputTypes = std::tuple<SensorData>
 */
template<typename IOBuilder, typename IOTuple>
struct ExtractInputTypes {
private:
    template<size_t... Is>
    static constexpr auto extract_impl(std::index_sequence<Is...>) {
        constexpr size_t num_inputs = IOBuilder::num_continuous_inputs + IOBuilder::num_synced_inputs;
        if constexpr (num_inputs == 0) {
            return std::tuple<>{};
        } else {
            return std::tuple<typename std::tuple_element_t<
                IOBuilder::input_indices()[Is], IOTuple
            >::DataMessage::Payload...>{};
        }
    }
    
public:
    static constexpr size_t num_inputs = IOBuilder::num_continuous_inputs + IOBuilder::num_synced_inputs;
    using type = decltype(extract_impl(std::make_index_sequence<num_inputs>{}));
};

template<typename IOBuilder, typename IOTuple>
using ExtractInputTypes_t = typename ExtractInputTypes<IOBuilder, IOTuple>::type;

/**
 * @brief Extract input wrapper types from I/O tuple (for ProcessorBase selection)
 * 
 * Given BuildIOTuple result, extracts the actual wrapper instances (not payloads).
 * This is needed for ProcessParam to correctly map types.
 * 
 * Example:
 *   IOTuple = std::tuple<ModuleOutput<...>, ContinuousInput<Registry, SensorData>, SyncedInputImpl<Registry, GPSData>>
 *   → InputWrappers = std::tuple<ContinuousInput<Registry, SensorData>, SyncedInputImpl<Registry, GPSData>>
 */
template<typename IOBuilder, typename IOTuple>
struct ExtractInputWrappers {
private:
    template<typename SeqType>
    struct extract_impl_helper;
    
    template<size_t... Is>
    struct extract_impl_helper<std::index_sequence<Is...>> {
        using type = std::tuple<std::tuple_element_t<
            IOBuilder::input_indices()[Is], IOTuple
        >...>;
    };
    
public:
    static constexpr size_t num_inputs = IOBuilder::num_continuous_inputs + IOBuilder::num_synced_inputs;
    using type = std::conditional_t<
        num_inputs == 0,
        std::tuple<>,
        typename extract_impl_helper<std::make_index_sequence<num_inputs>>::type
    >;
};

template<typename IOBuilder, typename IOTuple>
using ExtractInputWrappers_t = typename ExtractInputWrappers<IOBuilder, IOTuple>::type;

/**
 * @brief Get single input type (when num_inputs == 1)
 */
template<typename IOBuilder, typename IOTuple, bool HasInput = (IOBuilder::num_continuous_inputs + IOBuilder::num_synced_inputs) == 1>
struct SingleInputType {
    using type = void;  // No input or multiple inputs
};

template<typename IOBuilder, typename IOTuple>
struct SingleInputType<IOBuilder, IOTuple, true> {
    using type = std::tuple_element_t<0, ExtractInputTypes_t<IOBuilder, IOTuple>>;
};

template<typename IOBuilder, typename IOTuple>
using SingleInputType_t = typename SingleInputType<IOBuilder, IOTuple>::type;

} // namespace commrat
