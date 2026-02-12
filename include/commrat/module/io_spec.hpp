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

// ============================================================================
// Output Specifications
// ============================================================================

/**
 * @brief Single output specification
 * 
 * Specifies that a module produces exactly one output type.
 * Used with PeriodicInput, LoopInput, or Input<T> to define module behavior.
 * 
 * @tparam T The payload type to be published
 * 
 * **Process Signature:**
 * - With PeriodicInput: `void process(T& output)`
 * - With Input<U>: `void process(const U& input, T& output)`
 * - With Inputs<U, V>: `void process(const U& in1, const V& in2, T& output)`
 * 
 * @code
 * // Periodic sensor reading
 * class SensorModule : public Module<Registry, Output<TempData>, PeriodicInput> {
 * protected:
 *     void process(TempData& output) override {
 *         output.temperature = read_sensor();
 *     }
 * };
 * 
 * // Event-driven filtering
 * class FilterModule : public Module<Registry, Output<Filtered>, Input<Raw>> {
 * protected:
 *     void process(const Raw& input, Filtered& output) override {
 *         output.value = apply_filter(input.value);
 *     }
 * };
 * @endcode
 * 
 * @see Outputs for multiple output types
 * @see PeriodicInput, Input, Inputs for input specifications
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
 *                                        Input<RawData>> {
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
 * class LoggerModule : public Module<Registry, NoOutput, Input<LogData>> {
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
 * Automatically handles timestamp-based synchronization for sensor fusion.
 * 
 * **How Multi-Input Works:**
 * 1. First type in Inputs<T, U, V> is automatically PRIMARY (drives execution)
 * 2. Module blocks on PRIMARY input receive()
 * 3. Secondary inputs synchronized via getData(primary_timestamp, tolerance)
 * 4. All inputs time-aligned before process() is called
 * 
 * @tparam Ts... The payload types to receive (first is primary)
 * 
 * **Process Signature:**
 * - Single output: `void process(const T& in1, const U& in2, ..., OutputType& output)`
 * - Multi output: `void process(const T& in1, const U& in2, ..., O1& out1, O2& out2, ...)`
 * 
 * **Configuration:**
 * @code
 * ModuleConfig fusion_config{
 *     .name = "SensorFusion",
 *     .system_id = 30,
 *     .instance_id = 1,
 *     .period = Milliseconds(10),       // Primary input rate (100Hz)
 *     .input_sources = {
 *         {10, 1},  // IMU source (primary)
 *         {11, 1}   // GPS source (secondary)
 *     },
 *     .sync_tolerance_ns = 50'000'000   // 50ms tolerance for getData
 * };
 * @endcode
 * 
 * @code
 * // Fuse high-rate IMU with low-rate GPS
 * class FusionModule : public Module<Registry, 
 *                                    Output<FusedData>,
 *                                    Inputs<IMUData, GPSData>> {  // IMU first = primary
 * protected:
 *     void process(const IMUData& imu,      // Blocking receive (100Hz)
 *                  const GPSData& gps,      // getData at imu.timestamp (10Hz)
 *                  FusedData& output) override {
 *         // Check freshness
 *         if (!has_new_data<1>()) {
 *             std::cout << "GPS data is stale\n";
 *         }
 *         output = ekf_update(imu, gps);
 *     }
 * };
 * @endcode
 * 
 * @note Secondary inputs use HistoricalMailbox with circular buffering
 * @note Metadata accessors available: get_input_metadata<N>(), has_new_data<N>()
 * @see HistoricalMailbox for getData implementation details
 * @see InputMetadata for metadata structure
 */
template<typename... Ts>
struct Inputs {
    using PayloadTypes = std::tuple<Ts...>;
    static constexpr size_t count = sizeof...(Ts);
    
    static_assert(count > 0, "Inputs<> requires at least one type");
};

/**
 * @brief Primary input designation for multi-input synchronization (Phase 6)
 * 
 * When a module has multiple continuous inputs (Inputs<T, U, V>), one must be
 * designated as PRIMARY. The primary input drives execution - the module blocks
 * waiting for primary input, then fetches time-synchronized secondary inputs
 * using getData(timestamp).
 * 
 * @tparam T The payload type of the primary input (must be in Inputs<...>)
 * 
 * Example:
 * @code
 * // IMU runs at 100Hz (fast), GPS at 10Hz (slow)
 * // Use IMU as primary to maintain high update rate
 * class SensorFusion : public Module<Registry,
 *                                    Output<FusedPose>,
 *                                    Inputs<IMUData, GPSData>,
 *                                    PrimaryInput<IMUData>> {
 *     FusedPose process(const IMUData& imu,      // PRIMARY - received (blocking)
 *                       const GPSData& gps)       // SECONDARY - fetched at imu.timestamp
 *         override {
 *         // Both inputs time-aligned to imu.timestamp
 *         return ekf_update(imu, gps);
 *     }
 * };
 * @endcode
 */
template<typename T>
struct PrimaryInput {
    using PayloadType = T;
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
struct is_primary_input : std::false_type {};

template<typename T>
struct is_primary_input<PrimaryInput<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_primary_input_v = is_primary_input<T>::value;

template<typename T>
concept ValidInputSpec = is_periodic_input_v<T> || 
                         is_loop_input_v<T> || 
                         is_single_input_v<T> || 
                         is_multi_input_v<T>;

// --- Combined Concepts ---

template<typename T>
concept PeriodicOrLoop = is_periodic_input_v<T> || is_loop_input_v<T>;

template<typename T>
concept HasContinuousInput = is_single_input_v<T> || 
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

template<typename... Ts>
struct InputCount<Inputs<Ts...>> {
    static constexpr size_t value = sizeof...(Ts);
};

template<typename T>
inline constexpr size_t InputCount_v = InputCount<T>::value;

// ============================================================================
// Input/Output Normalization
// ============================================================================

/**
 * @brief Pass-through for input specifications (no normalization needed)
 */
template<typename T>
struct NormalizeInput {
    using Type = T;  // Default: pass through
};

template<typename T>
using NormalizeInput_t = typename NormalizeInput<T>::Type;

/**
 * @brief Maps raw output types to Output<T> format
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
 * @brief Get the single payload type from Input<T>
 */
template<typename InputSpec>
struct SingleInputType {
    using Type = void;  // No input type for PeriodicInput/LoopInput
};

template<typename T>
struct SingleInputType<Input<T>> {
    using Type = T;
};

template<typename InputSpec>
using SingleInputType_t = typename SingleInputType<InputSpec>::Type;

// ============================================================================
// Primary Input Index Extraction (Phase 6)
// ============================================================================

/**
 * @brief Extract the index of the primary input type within Inputs<Ts...>
 * 
 * Used for compile-time validation and runtime lookup of the primary input
 * mailbox in multi-input modules.
 * 
 * @tparam PrimaryT The payload type designated as primary (from PrimaryInput<T>)
 * @tparam InputsTuple The tuple of input types (from Inputs<Ts...>::PayloadTypes)
 * 
 * Example:
 * @code
 * using Inputs = Inputs<IMUData, GPSData, LidarData>;
 * using Primary = PrimaryInput<GPSData>;
 * 
 * // Find GPSData index in (IMUData, GPSData, LidarData)
 * constexpr size_t idx = PrimaryInputIndex<GPS Data,
 *                                          typename Inputs::PayloadTypes>::value;
 * // idx == 1
 * @endcode
 */
template<typename PrimaryT, typename InputsTuple>
struct PrimaryInputIndex;

// Base case: Primary type not found - compile error
template<typename PrimaryT>
struct PrimaryInputIndex<PrimaryT, std::tuple<>> {
    static_assert(sizeof(PrimaryT) == 0, 
                  "PrimaryInput type not found in Inputs<...> list. "
                  "Ensure PrimaryInput<T> specifies a type T that exists in Inputs<Ts...>.");
};

// Recursive case: Primary type matches first type in tuple
template<typename PrimaryT, typename... Rest>
struct PrimaryInputIndex<PrimaryT, std::tuple<PrimaryT, Rest...>> {
    static constexpr size_t value = 0;
};

// Recursive case: Primary type doesn't match, continue searching
template<typename PrimaryT, typename First, typename... Rest>
struct PrimaryInputIndex<PrimaryT, std::tuple<First, Rest...>> {
    static constexpr size_t value = 1 + PrimaryInputIndex<PrimaryT, std::tuple<Rest...>>::value;
};

template<typename PrimaryT, typename InputsTuple>
inline constexpr size_t PrimaryInputIndex_v = PrimaryInputIndex<PrimaryT, InputsTuple>::value;

/**
 * @brief Validate that PrimaryInput<T> is compatible with Inputs<Ts...>
 * 
 * Ensures compile-time safety: the primary input type must be one of the
 * input types in the Inputs<...> specification.
 * 
 * @tparam PrimaryInputSpec Must be PrimaryInput<T>
 * @tparam InputSpec Must be Inputs<Ts...> where T is in Ts...
 */
template<typename PrimaryInputSpec, typename InputSpec>
struct ValidatePrimaryInput {
    static_assert(is_primary_input_v<PrimaryInputSpec>,
                  "PrimaryInputSpec must be PrimaryInput<T>");
    
    static_assert(is_multi_input_v<InputSpec>,
                  "InputSpec must be Inputs<Ts...> when using PrimaryInput<T>");
    
    using PrimaryPayload = typename PrimaryInputSpec::PayloadType;
    using InputsPayloadTypes = typename InputSpec::PayloadTypes;
    
    // This will trigger compile error if PrimaryPayload not in InputsPayloadTypes
    static constexpr size_t primary_index = 
        PrimaryInputIndex_v<PrimaryPayload, InputsPayloadTypes>;
    
    static constexpr bool value = true;
};

template<typename PrimaryInputSpec, typename InputSpec>
inline constexpr bool ValidatePrimaryInput_v = 
    ValidatePrimaryInput<PrimaryInputSpec, InputSpec>::value;

} // namespace commrat
