#pragma once

/**
 * @file io_spec.hpp
 * @brief Complete I/O specification system for Module<>
 * 
 * Provides:
 * 1. I/O specification tags (Output<T>, Input<T>, Period<D>, SyncedInput<T>)
 * 2. Type traits for specifications and instances
 * 3. Execution mode validation and inference
 * 4. BuildIOTuple for compile-time I/O tuple construction
 * 
 * ============================================================================
 * EXECUTION MODELS (Auto-Inferred)
 * ============================================================================
 * 
 * CommRaT modules have three execution models based on I/O specifications:
 * 
 * 1. INPUT-DRIVEN (has Input<T>)
 *    - Spec: Module<Registry, Output<Data>, Input<SensorData>>
 *    - Execution: Blocked on input's receive() - data arrival drives execution
 *    - Use case: Data processing pipelines, sensor fusion
 * 
 * 2. TIMER-DRIVEN (has Period<D>, no Input)
 *    - Spec: Module<Registry, Output<Data>, Period<Milliseconds(100)>>
 *    - Execution: Sleep for period duration between iterations
 *    - Use case: Control loops, periodic polling, heartbeat generators
 * 
 * 3. LOOP-DRIVEN (no Input, no Period - AUTO-INFERRED)
 *    - Spec: Module<Registry, Output<Data>>
 *    - Execution: Continuous loop (maximum throughput, no sleep)
 *    - Use case: Maximum throughput processing
 * 
 * Constraints:
 * - At most ONE Input<T> (primary continuous input)
 * - At most ONE Period<D>
 * - Input<T> and Period<D> are mutually exclusive
 * - Multiple SyncedInput<T> allowed (secondary pull-model inputs)
 * - Multiple Output<T> allowed
 */

#include "commrat/module/io/input/cmd_input.hpp"
#include "commrat/module/io/input/continuous_input.hpp"
#include "commrat/module/io/input/synced_input.hpp"
#include "commrat/module/io/output/module_output.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/messaging/data_with_commands.hpp"
#include <tuple>
#include <type_traits>
#include <cstddef>

namespace commrat {

// Forward declarations
//template<typename CommratApp, typename T> class SyncedInput;

// ============================================================================
// I/O Specification Tags
// ============================================================================

/**
 * @brief Output specification tag
 * 
 * Specifies that a module produces data of type T.
 * T can be either a simple Data<Payload> or DataWithCommands<Payload, Cmds...>
 * 
 * @tparam T Output data type (Data or DataWithCommands)
 */
template<typename T>
struct Output {
    using Type = T;
    static constexpr bool is_output = true;
};

/**
 * @brief Continuous input specification tag
 * 
 * Specifies that a module receives continuous data stream of type T.
 * Uses push model - data arrives asynchronously via subscription.
 * 
 * Module execution: Driven by incoming data (blocking receive)
 * 
 * @tparam T Input data type (Data or DataWithCommands)
 * 
 * NOTE: Mutually exclusive with Period<>
 */
template<typename T>
struct Input {
    using Type = T;
    static constexpr bool is_input = true;
    static constexpr bool is_continuous = true;
};

/**
 * @brief Periodic execution specification tag
 * 
 * Specifies that a module runs at fixed period (timer-driven).
 * No continuous input - module generates data at regular intervals.
 * 
 * Module execution: Driven by timer (sleep until next period)
 * 
 * @tparam DefaultPeriod Default period duration (can be overridden in config)
 * 
 * NOTE: Mutually exclusive with Input<>
 */
template<auto DefaultPeriod>
struct Period {
    static constexpr auto default_period = DefaultPeriod;
    static constexpr bool is_periodic = true;
};

/**
 * @brief Synchronized input specification tag (OPTIONAL - for multi-input)
 * 
 * Specifies that a module pulls data via timestamp synchronization.
 * Uses pull model - get_data() queries producer's buffer.
 * 
 * This is SECONDARY to the primary input (Input<T> or Period<>).
 * Used for multi-rate sensor fusion.
 * 
 * @tparam T Input data type (Data or DataWithCommands)
 */
template<typename T>
struct SyncedInput {
    using Type = T;
    static constexpr bool is_input = true;
    static constexpr bool is_synced = true;
};

// ============================================================================
// Type Traits for I/O Specification Tags (for Module refactor)
// ============================================================================

template<typename T>
struct is_output : std::false_type {};

template<typename T>
struct is_output<Output<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_output_v = is_output<T>::value;

template<typename T>
struct is_input : std::false_type {};

template<typename T>
struct is_input<Input<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_input_v = is_input<T>::value;

template<typename T>
struct is_synced_input : std::false_type {};

template<typename T>
struct is_synced_input<SyncedInput<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_synced_input_v = is_synced_input<T>::value;

template<typename T>
struct is_period : std::false_type {};

template<auto DefaultPeriod>
struct is_period<Period<DefaultPeriod>> : std::true_type {};

template<typename T>
inline constexpr bool is_period_v = is_period<T>::value;

// ============================================================================
// Type Traits for I/O Instance Classes (for input_infrastructure.hpp)
// ============================================================================

/**
 * @brief Check if type is ContinuousInput instance
 */
template<typename T>
struct is_continuous_input : std::false_type {};

template<typename CommratApp, typename T>
struct is_continuous_input<ContinuousInput<CommratApp, T>> : std::true_type {};

template<typename T>
inline constexpr bool is_continuous_input_v = is_continuous_input<T>::value;

/**
 * @brief Check if type is SyncedInputImpl instance
 */
template<typename T>
struct is_synced_input_instance : std::false_type {};

template<typename CommratApp, typename T>
struct is_synced_input_instance<SyncedInputImpl<CommratApp, T>> : std::true_type {};

template<typename T>
inline constexpr bool is_synced_input_instance_v = is_synced_input_instance<T>::value;

/**
 * @brief Check if type is ModuleOutput instance
 */
template<typename T>
struct is_module_output : std::false_type {};

template<typename CommratApp, typename T>
struct is_module_output<ModuleOutput<CommratApp, T>> : std::true_type {};

template<typename T>
inline constexpr bool is_module_output_v = is_module_output<T>::value;

// ============================================================================
// Tuple Utilities (for input_infrastructure.hpp tuple-based I/O)
// ============================================================================

/**
 * @brief Find index of ContinuousInput instance in tuple
 */
template<typename Tuple, std::size_t I = 0>
struct find_continuous_input_index {
    static constexpr std::size_t value = 
        (I < std::tuple_size_v<Tuple>) ?
            (is_continuous_input_v<std::tuple_element_t<I, Tuple>> ? I : 
             find_continuous_input_index<Tuple, I + 1>::value) :
            std::tuple_size_v<Tuple>;
};

template<typename Tuple>
inline constexpr std::size_t find_continuous_input_index_v = find_continuous_input_index<Tuple>::value;

/**
 * @brief Count ContinuousInput instances in tuple
 */
template<typename Tuple, std::size_t I = 0>
struct count_continuous_inputs {
    static constexpr std::size_t value = 
        (I < std::tuple_size_v<Tuple>) ?
            (is_continuous_input_v<std::tuple_element_t<I, Tuple>> ? 1 : 0) +
            count_continuous_inputs<Tuple, I + 1>::value :
            0;
};

template<typename Tuple>
inline constexpr std::size_t count_continuous_inputs_v = count_continuous_inputs<Tuple>::value;

/**
 * @brief Concept: Input tuple must have at most one ContinuousInput
 */
template<typename Tuple>
concept ValidInputTuple = requires {
    requires count_continuous_inputs_v<Tuple> <= 1;
};

// ============================================================================
// Execution Mode Validation and Inference
// ============================================================================

/**
 * @brief Determine execution mode from I/O specifications
 * 
 * Rules (in priority order):
 * 1. If Input<T> present → Input-driven (continuous input)
 * 2. Else if Period<D> present → Timer-driven (periodic)
 * 3. Else → Loop-driven (maximum throughput)
 * 
 * Validation:
 * - At most one Input<T>
 * - At most one Period<D>
 * - Input<T> and Period<D> are mutually exclusive
 */
template<typename... IOSpecs>
struct ValidateExecutionMode {
    static constexpr size_t num_inputs = (is_input_v<IOSpecs> + ...);
    static constexpr size_t num_periods = (is_period_v<IOSpecs> + ...);
    
    // Validation
    static_assert(num_inputs <= 1, "Module can have at most one Input<T>");
    static_assert(num_periods <= 1, "Module can have at most one Period<D>");
    static_assert(num_inputs == 0 || num_periods == 0,
                  "Module cannot have both Input<T> and Period<D>");
    
    // Execution mode inference
    static constexpr bool is_input_driven = (num_inputs > 0);
    static constexpr bool is_timer_driven = (num_periods > 0) && !is_input_driven;
    static constexpr bool is_loop_driven = !is_input_driven && !is_timer_driven;
    
    static constexpr bool valid = true;
};

// ============================================================================
// I/O Instance Creation (for Module refactor)
// ============================================================================

/**
 * @brief Create I/O instance type from specification tag
 * 
 * Converts Output<T> → ModuleOutput<Registry, T>
 *         Input<T> → ContinuousInput<Registry, T>
 *         SyncedInput<T> → commrat::SyncedInput<Registry, T>
 *         Period<D> → void (no instance, just timer config)
 */
template<typename Registry, typename IOSpec>
struct CreateIOInstance;

// Output
template<typename Registry, typename T>
struct CreateIOInstance<Registry, Output<T>> {
    using type = ModuleOutput<Registry, T>;
    using spec_type = Output<T>;
};

// Continuous Input
template<typename Registry, typename T>
struct CreateIOInstance<Registry, Input<T>> {
    using type = ContinuousInput<Registry, T>;
    using spec_type = Input<T>;
};

// Synced Input
template<typename Registry, typename T>
struct CreateIOInstance<Registry, SyncedInput<T>> {
    using type = SyncedInputImpl<Registry, T>;
    using spec_type = SyncedInput<T>;
};

// Period (no instance - just config)
template<typename Registry, auto DefaultPeriod>
struct CreateIOInstance<Registry, Period<DefaultPeriod>> {
    using type = void;
    using spec_type = Period<DefaultPeriod>;
    static constexpr auto default_period = DefaultPeriod;
};

template<typename Registry, typename IOSpec>
using CreateIOInstance_t = typename CreateIOInstance<Registry, IOSpec>::type;

// ============================================================================
// I/O Tuple Builder (for Module refactor)
// ============================================================================

/**
 * @brief Build compile-time tuple of I/O instances
 * 
 * Input: IOSpecs... (Output<T>, Input<U>, SyncedInput<V>, Period<D>)
 * Output: std::tuple<ModuleOutput<T>, ContinuousInput<U>, SyncedInput<V>>
 *         (excludes void types from Period)
 * 
 * Execution mode is automatically inferred:
 * - Input<T> present → Input-driven
 * - Period<D> present (no Input) → Timer-driven
 * - Neither → Loop-driven (maximum throughput)
 */
template<typename Registry, typename... IOSpecs>
struct BuildIOTuple {
private:
    // Validate execution mode
    using Validator = ValidateExecutionMode<IOSpecs...>;
    static_assert(Validator::valid, "Invalid I/O specification");
    
    // Convert each IOSpec to instance type
    template<typename IOSpec>
    using ToInstance = CreateIOInstance_t<Registry, IOSpec>;
    
    // Filter out void types
    template<typename T>
    struct is_not_void : std::bool_constant<!std::is_void_v<T>> {};
    
    // Helper to filter tuple - uses std::declval for type-only deduction (no construction)
    template<typename... Ts>
    static auto filter_void(std::tuple<Ts...>*) -> decltype(
        std::tuple_cat(
            std::declval<std::conditional_t<
                is_not_void<Ts>::value,
                std::tuple<Ts>,
                std::tuple<>
            >>()...
        )
    );
    
public:
    // Create tuple of all instances (including void)
    using AllInstances = std::tuple<ToInstance<IOSpecs>...>;
    
    // Filter out void instances
    using type = decltype(filter_void(static_cast<AllInstances*>(nullptr)));
    
    // Metadata
    static constexpr size_t num_outputs = (is_output_v<IOSpecs> + ...);
    static constexpr size_t num_continuous_inputs = (is_input_v<IOSpecs> + ...);
    static constexpr size_t num_synced_inputs = (is_synced_input_v<IOSpecs> + ...);
    static constexpr bool has_period = (is_period_v<IOSpecs> || ...);
    static constexpr bool has_continuous_input = num_continuous_inputs > 0;
    
    // Execution mode (auto-inferred, exactly one is true)
    static constexpr bool is_input_driven = Validator::is_input_driven;
    static constexpr bool is_timer_driven = Validator::is_timer_driven;
    static constexpr bool is_loop_driven = Validator::is_loop_driven;
    
    static constexpr size_t total_io = sizeof...(IOSpecs);
    
    // ========================================================================
    // Metadata Struct (Clean Interface for Module2)
    // ========================================================================
    
    /**
     * @brief Consolidated metadata for I/O configuration
     * 
     * Provides all compile-time metadata in one clean struct.
     * Usage: using IO = IOBuilder::Meta;
     *        IO::num_outputs, IO::is_loop_driven, IO::primary_input_index
     */
    struct Meta {
        // Numeric metadata
        static constexpr size_t num_outputs = BuildIOTuple::num_outputs;
        static constexpr size_t num_continuous_inputs = BuildIOTuple::num_continuous_inputs;
        static constexpr size_t num_synced_inputs = BuildIOTuple::num_synced_inputs;
        static constexpr size_t num_inputs = num_continuous_inputs + num_synced_inputs;
        static constexpr size_t total_io = BuildIOTuple::total_io;
        static constexpr bool has_inputs = (num_inputs > 0);
        static constexpr bool has_outputs = (num_outputs > 0);
        
        // Execution mode
        static constexpr bool is_input_driven = BuildIOTuple::is_input_driven;
        static constexpr bool is_timer_driven = BuildIOTuple::is_timer_driven;
        static constexpr bool is_loop_driven = BuildIOTuple::is_loop_driven;
        
        // Primary input detection (for multi-input synchronization)
    private:
        template<size_t Index = 0>
        static constexpr size_t find_primary_input_index() {
            if constexpr (Index >= num_inputs) {
                return num_inputs;  // Not found
            } else {
                using InputType = std::tuple_element_t<input_indices()[Index], type>;
                if constexpr (is_continuous_input_v<InputType>) {
                    return Index;
                } else {
                    return find_primary_input_index<Index + 1>();
                }
            }
        }
        
    public:
        static constexpr size_t primary_input_index = find_primary_input_index();
        static constexpr bool has_primary_input = (primary_input_index < num_inputs);
        
        // Period extraction (for timer-driven modules)
    private:
        // Helper to extract period from a single spec
        template<typename Spec>
        struct PeriodExtractor {
            static constexpr Milliseconds value = Milliseconds(0);
        };
        
        template<auto DefaultPeriod>
        struct PeriodExtractor<Period<DefaultPeriod>> {
            // Handle both integer milliseconds and duration types
            static constexpr auto value = []() {
                if constexpr (std::is_integral_v<decltype(DefaultPeriod)>) {
                    return Milliseconds(DefaultPeriod);
                } else {
                    return DefaultPeriod;
                }
            }();
        };
        
        // Recursive find: return first non-zero period
        template<typename First, typename... Rest>
        static constexpr auto find_period_impl() {
            constexpr auto first_period = PeriodExtractor<First>::value;
            if constexpr (first_period.count() > 0) {
                return first_period;
            } else if constexpr (sizeof...(Rest) > 0) {
                return find_period_impl<Rest...>();
            } else {
                return Milliseconds(0);
            }
        }
        
    public:
        static constexpr auto period = find_period_impl<IOSpecs...>();
        
        // Type extraction
        using OutputTypes = ExtractOutputTypes_t<BuildIOTuple, type>;
        using InputTypes = ExtractInputTypes_t<BuildIOTuple, type>;
        using InputWrappers = ExtractInputWrappers_t<BuildIOTuple, type>;  // Wrapper types for ProcessorBase
        using SingleOutputType = SingleOutputType_t<BuildIOTuple, type>;
        using SingleInputType = SingleInputType_t<BuildIOTuple, type>;
    };
    
    // ========================================================================
    // Compile-Time Index Sequences (Separate Outputs from Inputs in tuple)
    // ========================================================================
    
    /**
     * @brief Build output indices array at compile-time
     * 
     * Maps output logical indices (0, 1, ..., num_outputs-1) to tuple indices.
     * 
     * Example: Output<A>, Output<B>, Input<C>, Input<D>
     *   -> output_indices = [0, 1]  (outputs at tuple positions 0, 1)
     */
    template<size_t... Is>
    static constexpr auto build_output_indices(std::index_sequence<Is...>) {
        std::array<size_t, num_outputs> result{};
        size_t out_idx = 0;
        ((void)[&]() {
            using IOType = std::tuple_element_t<Is, type>;
            if constexpr (is_module_output_v<IOType>) {
                result[out_idx++] = Is;
            }
        }(), ...);
        return result;
    }
    
    /**
     * @brief Build input indices array at compile-time
     * 
     * Maps input logical indices (0, 1, ..., num_inputs-1) to tuple indices.
     * 
     * Example: Output<A>, Output<B>, Input<C>, Input<D>
     *   -> input_indices = [2, 3]  (inputs at tuple positions 2, 3)
     */
    template<size_t... Is>
    static constexpr auto build_input_indices(std::index_sequence<Is...>) {
        constexpr size_t num_inputs = num_continuous_inputs + num_synced_inputs;
        std::array<size_t, num_inputs> result{};
        size_t in_idx = 0;
        ((void)[&]() {
            using IOType = std::tuple_element_t<Is, type>;
            if constexpr (is_continuous_input_v<IOType> || is_synced_input_instance_v<IOType>) {
                result[in_idx++] = Is;
            }
        }(), ...);
        return result;
    }
    
    /**
     * @brief Get output indices as compile-time array
     * 
     * Usage: auto indices = BuildIOTuple::output_indices();
     */
    static constexpr auto output_indices() {
        return build_output_indices(std::make_index_sequence<std::tuple_size_v<type>>{});
    }
    
    /**
     * @brief Get input indices as compile-time array
     * 
     * Usage: auto indices = BuildIOTuple::input_indices();
     */
    static constexpr auto input_indices() {
        return build_input_indices(std::make_index_sequence<std::tuple_size_v<type>>{});
    }
    
    // ========================================================================
    // Generic Index-Based Accessors
    // ========================================================================
    
    /**
     * @brief Get output by logical index (helper for Module)
     * 
     * Maps output logical index (0-based among outputs) to tuple index.
     * 
     * @tparam OutputIndex Logical output index (0 to num_outputs-1)
     * @param tuple I/O tuple instance
     * @return Reference to output at logical index
     */
    template<size_t OutputIndex, typename IOTupleType>
        requires (OutputIndex < num_outputs)
    static constexpr auto& get_output(IOTupleType&& tuple) {
        constexpr auto indices = output_indices();
        constexpr size_t tuple_index = indices[OutputIndex];
        return std::get<tuple_index>(std::forward<IOTupleType>(tuple));
    }
    
    /**
     * @brief Get input by logical index (helper for Module)
     * 
     * Maps input logical index (0-based among inputs) to tuple index.
     * 
     * @tparam InputIndex Logical input index (0 to num_inputs-1)
     * @param tuple I/O tuple instance
     * @return Reference to input at logical index
     */
    template<size_t InputIndex, typename IOTupleType>
        requires (InputIndex < (num_continuous_inputs + num_synced_inputs))
    static constexpr auto& get_input(IOTupleType&& tuple) {
        constexpr auto indices = input_indices();
        constexpr size_t tuple_index = indices[InputIndex];
        return std::get<tuple_index>(std::forward<IOTupleType>(tuple));
    }
};

template<typename Registry, typename... IOSpecs>
using BuildIOTuple_t = typename BuildIOTuple<Registry, IOSpecs...>::type;

} // namespace commrat
