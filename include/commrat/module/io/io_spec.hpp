#pragma once

/**
 * @file io_spec.hpp
 * @brief I/O specifications using new Input/Output classes
 * 
 * Clean specs that map directly to CmdInput, ContinuousInput, SyncedInput, BufferedOutput
 * No more template parameter lists - direct class references
 * 
 * ============================================================================
 * THREE EXECUTION MODELS
 * ============================================================================
 * 
 * CommRaT modules support three execution models based on input configuration:
 * 
 * 1. INPUT-DRIVEN EXECUTION (has ContinuousInput)
 *    - Input spec: Input<T> or Inputs<T1, T2, ...>
 *    - Execution: Blocked on primary input's receive()
 *    - Period: Ignored (input arrival drives execution)
 *    - Use case: Data processing pipelines, sensor fusion
 * 
 * 2. PERIODIC EXECUTION (no ContinuousInput + period configured)
 *    - Input spec: Omit input spec entirely, or CmdOnlyInput<T>, or only SyncedInputs
 *    - Config: config.period = std::chrono::milliseconds{100}
 *    - Execution: Sleep for period duration between iterations
 *    - Use case: Control loops, periodic polling, heartbeat generators
 *    - Example: Module<Output<Data>> with config.period set
 * 
 * 3. FREE-RUNNING LOOP (no ContinuousInput + no period)
 *    - Input spec: Omit input spec entirely, or CmdOnlyInput<T>, or only SyncedInputs  
 *    - Config: config.period = std::nullopt
 *    - Execution: No sleep - runs as fast as possible
 *    - Use case: Maximum throughput processing, busy-wait scenarios
 *    - Example: Module<Output<Data>> with config.period = std::nullopt
 * 
 * Key Constraint: At most ONE ContinuousInput per module
 * - 0 ContinuousInputs: Periodic or free-running (depends on period config)
 * - 1 ContinuousInput: Input-driven execution
 * - 2+ ContinuousInputs: Compile error (ValidInputTuple concept enforces this)
 */

#include <tuple>
#include <type_traits>

namespace commrat {

// ============================================================================
// Forward Declarations
// ============================================================================

template<typename CommratApp, typename T> class CmdInput;
template<typename CommratApp, typename T> class ContinuousInput;
template<typename CommratApp, typename T> class SyncedInput;
template<typename CommratApp, typename T, std::size_t SLOTS> class BufferedOutput;

// ============================================================================
// Output Specifications
// ============================================================================

/**
 * @brief Single buffered output
 * 
 * Module produces one output type using BufferedOutput<T>
 */
template<typename T>
struct Output {
    using Type = T;
    static constexpr std::size_t count = 1;
    
    template<typename CommratApp, std::size_t SLOTS = 100>
    using OutputClass = BufferedOutput<CommratApp, T, SLOTS>;
};

/**
 * @brief Multiple buffered outputs
 * 
 * Module produces multiple output types, each with own BufferedOutput
 */
template<typename... Ts>
struct Outputs {
    using Types = std::tuple<Ts...>;
    static constexpr std::size_t count = sizeof...(Ts);
    
    static_assert(count > 0, "Outputs<> requires at least one type");
};

/// No output - sink module (logger, storage, etc.)
struct NoOutput {
    static constexpr std::size_t count = 0;
};

// ============================================================================
// Input Specifications  
// ============================================================================

/**
 * @brief Single continuous input (push model)
 * 
 * Module receives continuous stream using ContinuousInput<T>
 */
template<typename T>
struct Input {
    using Type = T;
    static constexpr std::size_t count = 1;
    
    template<typename CommratApp>
    using InputClass = ContinuousInput<CommratApp, T>;
};

/**
 * @brief Multiple inputs (mixed continuous + synced)
 * 
 * First type is primary (drives execution via ContinuousInput)
 * Remaining types are secondary (synchronized via SyncedInput)
 */
template<typename... Ts>
struct Inputs {
    using Types = std::tuple<Ts...>;
    static constexpr std::size_t count = sizeof...(Ts);
    
    using PrimaryType = std::tuple_element_t<0, Types>;
    
    static_assert(count > 0, "Inputs<> requires at least one type");
};

/**
 * @brief Command-only input (no data)
 * 
 * Module can send commands to producer but doesn't receive data
 */
template<typename T>
struct CmdOnlyInput {
    using Type = T;
    static constexpr std::size_t count = 0;  // Doesn't participate in process()
    
    template<typename CommratApp>
    using InputClass = CmdInput<CommratApp, T>;
};

// ============================================================================
// Type Traits
// ============================================================================

/// Check if T is an output spec
template<typename T>
struct is_output_spec : std::false_type {};

template<typename T>
struct is_output_spec<Output<T>> : std::true_type {};

template<typename... Ts>
struct is_output_spec<Outputs<Ts...>> : std::true_type {};

template<>
struct is_output_spec<NoOutput> : std::true_type {};

template<typename T>
inline constexpr bool is_output_spec_v = is_output_spec<T>::value;

/// Check if T is an input spec (Input, Inputs, CmdOnlyInput)
/// Note: Modules can omit input spec entirely for periodic/free-running execution
template<typename T>
struct is_input_spec : std::false_type {};

template<typename T>
struct is_input_spec<Input<T>> : std::true_type {};

template<typename... Ts>
struct is_input_spec<Inputs<Ts...>> : std::true_type {};

template<typename T>
struct is_input_spec<CmdOnlyInput<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_input_spec_v = is_input_spec<T>::value;

/// Concept: Valid input specification or void (no input)
template<typename T>
concept InputSpecOrVoid = is_input_spec_v<T> || std::is_void_v<T>;

} // namespace commrat
