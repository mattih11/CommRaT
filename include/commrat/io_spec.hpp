/**
 * @file io_spec.hpp
 * @brief Input/Output specifications for multi-I/O modules
 * 
 * Defines tag types and concepts for specifying module inputs and outputs.
 * Supports both single and multiple inputs/outputs with full compile-time
 * type safety and zero runtime overhead.
 * 
 * @author CommRaT Development Team
 * @date February 6, 2026
 */

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <concepts>

namespace commrat {

// ============================================================================
// Forward Declarations
// ============================================================================

struct PeriodicInput;
struct LoopInput;
template<typename T> struct ContinuousInput;

// ============================================================================
// Output Specifications
// ============================================================================

/**
 * @brief Single output specification
 * 
 * Specifies that a module produces exactly one output type.
 * 
 * @tparam T The payload type to be published
 * 
 * Example:
 * @code
 * class SensorModule : public Module<Registry, Output<TempData>, PeriodicInput> {
 *     TempData process() override { return read_sensor(); }
 * };
 * @endcode
 */
template<typename T>
struct Output {
    using PayloadType = T;
    static constexpr size_t count = 1;
};

/**
 * @brief Multiple outputs specification
 * 
 * Specifies that a module produces multiple output types.
 * Each output type has its own subscriber list and is published independently.
 * 
 * @tparam Ts... The payload types to be published
 * 
 * Example:
 * @code
 * class ProcessingModule : public Module<Registry, 
 *                                        Outputs<ProcessedData, Diagnostics>,
 *                                        ContinuousInput<RawData>> {
 *     void process(const RawData& raw, ProcessedData& proc, Diagnostics& diag) override {
 *         proc = filter(raw);
 *         diag = calculate_stats(raw);
 *     }
 * };
 * @endcode
 */
template<typename... Ts>
struct Outputs {
    using PayloadTypes = std::tuple<Ts...>;
    static constexpr size_t count = sizeof...(Ts);
    
    static_assert(count > 0, "Outputs<> requires at least one type");
};

/**
 * @brief No output specification (sink module)
 * 
 * Specifies that a module does not publish any output.
 * Useful for logging, storage, or terminal modules.
 * 
 * Example:
 * @code
 * class LoggerModule : public Module<Registry, NoOutput, ContinuousInput<LogData>> {
 *     void process(const LogData& data) override {
 *         write_to_file(data);
 *         // No return value
 *     }
 * };
 * @endcode
 */
struct NoOutput {
    static constexpr size_t count = 0;
};

// ============================================================================
// Input Specifications
// ============================================================================

/**
 * @brief Single continuous input specification
 * 
 * Specifies that a module receives exactly one input type from another module.
 * The module subscribes to a producer and processes each received message.
 * 
 * @tparam T The payload type to receive
 * 
 * Example:
 * @code
 * class FilterModule : public Module<Registry, Output<FilteredData>, Input<RawData>> {
 *     FilteredData process(const RawData& input) override {
 *         return apply_filter(input);
 *     }
 * };
 * @endcode
 */
template<typename T>
struct Input {
    using PayloadType = T;
    static constexpr size_t count = 1;
};

/**
 * @brief Multiple continuous inputs specification
 * 
 * Specifies that a module receives multiple input types from different sources.
 * The module subscribes to multiple producers and processes messages as they arrive.
 * 
 * @tparam Ts... The payload types to receive
 * 
 * Example:
 * @code
 * class FusionModule : public Module<Registry, 
 *                                    Output<FusedData>,
 *                                    Inputs<IMUData, GPSData, LidarData>> {
 *     FusedData process(const IMUData& imu, 
 *                       const GPSData& gps, 
 *                       const LidarData& lidar) override {
 *         return fuse_sensors(imu, gps, lidar);
 *     }
 * };
 * @endcode
 */
template<typename... Ts>
struct Inputs {
    using PayloadTypes = std::tuple<Ts...>;
    static constexpr size_t count = sizeof...(Ts);
    
    static_assert(count > 0, "Inputs<> requires at least one type");
};

// ============================================================================
// Type Traits and Concepts
// ============================================================================

// --- Output Type Detection ---

template<typename T>
struct is_single_output : std::false_type {};

template<typename T>
struct is_single_output<Output<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_single_output_v = is_single_output<T>::value;

template<typename T>
struct is_multi_output : std::false_type {};

template<typename... Ts>
struct is_multi_output<Outputs<Ts...>> : std::true_type {};

template<typename T>
inline constexpr bool is_multi_output_v = is_multi_output<T>::value;

template<typename T>
struct is_no_output : std::false_type {};

template<>
struct is_no_output<NoOutput> : std::true_type {};

template<typename T>
inline constexpr bool is_no_output_v = is_no_output<T>::value;

template<typename T>
concept ValidOutputSpec = is_single_output_v<T> || 
                          is_multi_output_v<T> || 
                          is_no_output_v<T>;

// --- Input Type Detection ---

template<typename T>
struct is_periodic_input : std::false_type {};

template<>
struct is_periodic_input<PeriodicInput> : std::true_type {};

template<typename T>
inline constexpr bool is_periodic_input_v = is_periodic_input<T>::value;

template<typename T>
struct is_loop_input : std::false_type {};

template<>
struct is_loop_input<LoopInput> : std::true_type {};

template<typename T>
inline constexpr bool is_loop_input_v = is_loop_input<T>::value;

template<typename T>
struct is_continuous_input_legacy : std::false_type {};

template<typename T>
struct is_continuous_input_legacy<ContinuousInput<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_continuous_input_legacy_v = is_continuous_input_legacy<T>::value;

template<typename T>
struct is_single_input : std::false_type {};

template<typename T>
struct is_single_input<Input<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_single_input_v = is_single_input<T>::value;

template<typename T>
struct is_multi_input : std::false_type {};

template<typename... Ts>
struct is_multi_input<Inputs<Ts...>> : std::true_type {};

template<typename T>
inline constexpr bool is_multi_input_v = is_multi_input<T>::value;

template<typename T>
concept ValidInputSpec = is_periodic_input_v<T> || 
                         is_loop_input_v<T> || 
                         is_continuous_input_legacy_v<T> ||
                         is_single_input_v<T> || 
                         is_multi_input_v<T>;

// --- Combined Concepts ---

template<typename T>
concept PeriodicOrLoop = is_periodic_input_v<T> || is_loop_input_v<T>;

template<typename T>
concept HasContinuousInput = is_continuous_input_legacy_v<T> || 
                             is_single_input_v<T> || 
                             is_multi_input_v<T>;

// --- Count Helpers ---

/**
 * @brief Get the number of outputs in an OutputSpec
 */
template<typename T>
struct OutputCount {
    static constexpr size_t value = 1;  // Raw type = single output
};

template<typename T>
struct OutputCount<Output<T>> {
    static constexpr size_t value = 1;
};

template<typename... Ts>
struct OutputCount<Outputs<Ts...>> {
    static constexpr size_t value = sizeof...(Ts);
};

template<>
struct OutputCount<NoOutput> {
    static constexpr size_t value = 0;
};

template<typename T>
inline constexpr size_t OutputCount_v = OutputCount<T>::value;

/**
 * @brief Get the number of inputs in an InputSpec
 */
template<typename T>
struct InputCount {
    static constexpr size_t value = 0;  // PeriodicInput, LoopInput = no input
};

template<typename T>
struct InputCount<Input<T>> {
    static constexpr size_t value = 1;
};

template<typename T>
struct InputCount<ContinuousInput<T>> {
    static constexpr size_t value = 1;
};

template<typename... Ts>
struct InputCount<Inputs<Ts...>> {
    static constexpr size_t value = sizeof...(Ts);
};

template<typename T>
inline constexpr size_t InputCount_v = InputCount<T>::value;

// ============================================================================
// Backward Compatibility Mapping
// ============================================================================

/**
 * @brief Maps legacy input specifications to new Input<T> format
 * 
 * This allows existing code using ContinuousInput<T> to work seamlessly
 * with the new multi-I/O system.
 */
template<typename T>
struct NormalizeInput {
    using Type = T;  // Default: pass through
};

// Map ContinuousInput<T> -> Input<T>
template<typename T>
struct NormalizeInput<ContinuousInput<T>> {
    using Type = Input<T>;
};

template<typename T>
using NormalizeInput_t = typename NormalizeInput<T>::Type;

/**
 * @brief Maps legacy output specifications to new Output<T> format
 * 
 * For backward compatibility, a raw payload type T is treated as Output<T>.
 */
template<typename T>
struct NormalizeOutput {
    // If T is already an output spec, use it as-is
    // Otherwise, wrap it in Output<T>
    using Type = std::conditional_t<
        ValidOutputSpec<T>,
        T,
        Output<T>
    >;
};

template<typename T>
using NormalizeOutput_t = typename NormalizeOutput<T>::Type;

// ============================================================================
// Parameter Pack Expansion Helpers
// ============================================================================

/**
 * @brief Extract payload types from Inputs<Ts...> as a tuple
 */
template<typename InputSpec>
struct InputPayloadTypes {
    using Type = std::tuple<>;  // Default: no inputs
};

template<typename T>
struct InputPayloadTypes<Input<T>> {
    using Type = std::tuple<T>;
};

template<typename... Ts>
struct InputPayloadTypes<Inputs<Ts...>> {
    using Type = std::tuple<Ts...>;
};

template<typename InputSpec>
using InputPayloadTypes_t = typename InputPayloadTypes<InputSpec>::Type;

/**
 * @brief Extract payload types from Outputs<Ts...> as a tuple
 */
template<typename OutputSpec>
struct OutputPayloadTypes {
    using Type = std::tuple<>;  // Default: no outputs
};

template<typename T>
struct OutputPayloadTypes<Output<T>> {
    using Type = std::tuple<T>;
};

template<typename... Ts>
struct OutputPayloadTypes<Outputs<Ts...>> {
    using Type = std::tuple<Ts...>;
};

template<typename OutputSpec>
using OutputPayloadTypes_t = typename OutputPayloadTypes<OutputSpec>::Type;

/**
 * @brief Get the single payload type from Output<T> or legacy OutputDataT
 */
template<typename OutputSpec>
struct SingleOutputType {
    using Type = OutputSpec;  // Legacy: raw type is the output
};

template<typename T>
struct SingleOutputType<Output<T>> {
    using Type = T;
};

template<typename OutputSpec>
using SingleOutputType_t = typename SingleOutputType<OutputSpec>::Type;

/**
 * @brief Get the single payload type from Input<T> or ContinuousInput<T>
 */
template<typename InputSpec>
struct SingleInputType {
    using Type = void;  // No input type for PeriodicInput/LoopInput
};

template<typename T>
struct SingleInputType<Input<T>> {
    using Type = T;
};

template<typename T>
struct SingleInputType<ContinuousInput<T>> {
    using Type = T;
};

template<typename InputSpec>
using SingleInputType_t = typename SingleInputType<InputSpec>::Type;

} // namespace commrat
