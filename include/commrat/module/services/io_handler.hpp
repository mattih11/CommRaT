#pragma once

/**
 * @file io_handler.hpp
 * @brief I/O management service for Module2
 * 
 * Provides:
 * - I/O tuple storage and access
 * - Structured inputs/outputs via rfl::NamedTuple
 * - Input/output indexing helpers
 * - Separation of concerns from main Module2 class
 */

#include "commrat/module/io/io_spec.hpp"
#include "commrat/module/io/output_infrastructure.hpp"
// input_infrastructure.hpp contains legacy class - thin wrapper now in output_infrastructure.hpp
#include "commrat/platform/timestamp.hpp"
#include <tuple>

namespace commrat {

/**
 * @brief I/O Handler Service - Manages inputs and outputs
 * 
 * Provides structured access to I/O via named tuples:
 * - inputs()  -> rfl::NamedTuple with named fields
 * - outputs() -> rfl::NamedTuple with named fields
 * - get_input<N>() / get_output<N>() for index-based access
 * 
 * @tparam Registry The message registry type
 * @tparam IOSpecs The I/O specification types (Output<T>, Input<T>, etc.)
 */
template<typename Registry, typename... IOSpecs>
class IOHandler {
protected:
    // ========================================================================
    // I/O Builder Infrastructure
    // ========================================================================
    
    using IOBuilder = BuildIOTuple<Registry, IOSpecs...>;
    using IOTuple = typename IOBuilder::type;
    
    // Mixins for structured access (thin wrappers - delegate to IOBuilder)
    using OutputInfra = OutputInfrastructure<IOBuilder, IOTuple>;
    using InputInfra = InputInfrastructure<IOBuilder, IOTuple>;
    
    // ========================================================================
    // I/O Metadata
    // ========================================================================
    
    static constexpr size_t num_outputs = IOBuilder::num_outputs;
    static constexpr size_t num_continuous_inputs = IOBuilder::num_continuous_inputs;
    static constexpr size_t num_synced_inputs = IOBuilder::num_synced_inputs;
    static constexpr size_t num_inputs = num_continuous_inputs + num_synced_inputs;
    
    // ========================================================================
    // I/O Storage
    // ========================================================================
    
    std::optional<IOTuple> io_instances_;  // Initialized in initialize_io()
    
    // Helper to get tuple from optional
    IOTuple& get_io_tuple() { 
        return *io_instances_; 
    }
    
    const IOTuple& get_io_tuple() const { 
        return *io_instances_; 
    }
    
    // ========================================================================
    // Generic Accessors (Index-Based)
    // ========================================================================
    
    /**
     * @brief Get output at specific index (compile-time)
     */
    template<size_t OutputIndex>
    auto& get_output() {
        static_assert(OutputIndex < num_outputs, "Output index out of range");
        constexpr auto idx = IOBuilder::output_indices()[OutputIndex];
        return std::get<idx>(get_io_tuple());
    }
    
    template<size_t OutputIndex>
    const auto& get_output() const {
        static_assert(OutputIndex < num_outputs, "Output index out of range");
        constexpr auto idx = IOBuilder::output_indices()[OutputIndex];
        return std::get<idx>(get_io_tuple());
    }
    
    /**
     * @brief Get input at specific index (compile-time)
     */
    template<size_t InputIndex>
    auto& get_input() {
        static_assert(InputIndex < num_inputs, "Input index out of range");
        constexpr auto idx = IOBuilder::input_indices()[InputIndex];
        return std::get<idx>(get_io_tuple());
    }
    
    template<size_t InputIndex>
    const auto& get_input() const {
        static_assert(InputIndex < num_inputs, "Input index out of range");
        constexpr auto idx = IOBuilder::input_indices()[InputIndex];
        return std::get<idx>(get_io_tuple());
    }
    
    // ========================================================================
    // Structured Access (Named Tuples)
    // ========================================================================
    
    /**
     * @brief Get all outputs as named struct
     * 
     * Returns rfl::NamedTuple with fields named after payload types.
     * Example: outputs().sensor_data, outputs().filtered_data
     */
    auto outputs() {
        return OutputInfra::build_named_output_struct(get_io_tuple(), std::make_index_sequence<num_outputs>{});
    }
    
    auto outputs() const {
        return OutputInfra::build_named_output_struct(get_io_tuple(), std::make_index_sequence<num_outputs>{});
    }
    
    /**
     * @brief Get all inputs as named struct
     * 
     * Returns rfl::NamedTuple with fields named after payload types.
     * Example: inputs().sensor_data, inputs().gps_data
     */
    auto inputs() {
        return InputInfra::build_named_input_struct(get_io_tuple(), std::make_index_sequence<num_inputs>{});
    }
    
    auto inputs() const {
        return InputInfra::build_named_input_struct(get_io_tuple(), std::make_index_sequence<num_inputs>{});
    }
    
    // ========================================================================
    // I/O Operations
    // ========================================================================
    
    /**
     * @brief Publish all outputs to their subscribers
     */
    template<size_t... OutputIndices>
    void publish_outputs(std::index_sequence<OutputIndices...>) {
        // Get timestamp once for all outputs (consistent timestamp)
        Timestamp now = Time::now();
        
        // Publish each output
        (get_output<OutputIndices>().publish_workspace(now), ...);
    }
    
    /**
     * @brief Fetch data for all inputs (continuous poll, synced get_data)
     */
    template<size_t... InputIndices>
    void fetch_inputs(std::index_sequence<InputIndices...>) {
        (fetch_single_input<InputIndices>(), ...);
    }
    
    /**
     * @brief Get reference to input payload for process() - ZERO-COPY!
     */
    template<size_t InputIndex>
    decltype(auto) get_input_data() {
        return get_input<InputIndex>().get_payload();
    }
    
    /**
     * @brief Get reference to output workspace for process() - ZERO-COPY!
     */
    template<size_t OutputIndex>
    decltype(auto) get_output_data() {
        return get_output<OutputIndex>().get_workspace();
    }
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * @brief Initialize all I/O instances
     */
    template<typename ModuleConfig, typename WorkMailbox>
    void initialize_io(const ModuleConfig& config, WorkMailbox& work_mailbox) {
        initialize_outputs(std::make_index_sequence<num_outputs>{}, config);
        initialize_inputs(std::make_index_sequence<num_inputs>{}, config, work_mailbox);
    }
    
    /**
     * @brief Initialize all outputs
     */
    template<size_t... OutputIndices, typename ModuleConfig>
    void initialize_outputs(std::index_sequence<OutputIndices...>, const ModuleConfig& config) {
        (initialize_output<OutputIndices>(config), ...);
    }
    
    /**
     * @brief Initialize single output at index
     */
    template<size_t OutputIndex, typename ModuleConfig>
    void initialize_output(const ModuleConfig& config) {
        auto& output = get_output<OutputIndex>();
        
        // Get system_id and instance_id from config
        uint8_t sys_id, inst_id;
        if (config.has_multi_output_config()) {
            sys_id = config.system_id(OutputIndex);
            inst_id = config.instance_id(OutputIndex);
        } else {
            sys_id = config.system_id();
            inst_id = config.instance_id();
        }
        
        // Get sync tolerance from config (with default)
        auto tolerance = config.has_multi_input_config() 
            ? config.sync_tolerance() 
            : Milliseconds(50);
        
        output.initialize(sys_id, inst_id, tolerance);
    }
    
    /**
     * @brief Initialize all inputs
     */
    template<size_t... InputIndices, typename ModuleConfig, typename WorkMailbox>
    void initialize_inputs(std::index_sequence<InputIndices...>, const ModuleConfig& config, WorkMailbox& work_mailbox) {
        (initialize_input<InputIndices>(config, work_mailbox), ...);
    }
    
    /**
     * @brief Initialize single input at index
     */
    template<size_t InputIndex, typename ModuleConfig, typename WorkMailbox>
    void initialize_input(const ModuleConfig& config, WorkMailbox& work_mailbox) {
        auto& input = get_input<InputIndex>();
        
        // Get source addresses from config
        uint8_t src_sys_id, src_inst_id;
        
        if constexpr (num_inputs == 1) {
            // Single input - use source_system_id/source_instance_id
            src_sys_id = config.source_system_id();
            src_inst_id = config.source_instance_id();
        } else {
            // Multi-input - use input_sources array
            const auto& sources = config.input_sources();
            src_sys_id = sources[InputIndex].system_id;
            src_inst_id = sources[InputIndex].instance_id;
        }
        
        // Initialize input with work_mailbox and source addresses
        // TODO: Inputs are constructed directly in tuple, not post-initialized
        // This will be implemented when tuple construction is working
        // input.initialize(work_mailbox, src_sys_id, src_inst_id);
    }
    
    // ========================================================================
    // Subscription Management
    // ========================================================================
    
    /**
     * @brief Subscribe all inputs to their producers
     */
    template<size_t... InputIndices>
    void subscribe_inputs(std::index_sequence<InputIndices...>) {
        (subscribe_input<InputIndices>(), ...);
    }
    
    /**
     * @brief Subscribe single input
     */
    template<size_t InputIndex>
    void subscribe_input() {
        auto& input = get_input<InputIndex>();
        
        // Only ContinuousInput needs subscription (SyncedInput uses get_data)
        using InputWrapper = std::decay_t<decltype(input)>;
        if constexpr (is_continuous_input_v<InputWrapper>) {
            Milliseconds actual_period;
            if (!input.subscribe(actual_period)) {
                // TODO: Handle subscription failure
            }
        }
    }
    
    /**
     * @brief Unsubscribe all inputs from their producers
     */
    template<size_t... InputIndices>
    void unsubscribe_inputs(std::index_sequence<InputIndices...>) {
        (unsubscribe_input<InputIndices>(), ...);
    }
    
    /**
     * @brief Unsubscribe single input
     */
    template<size_t InputIndex>
    void unsubscribe_input() {
        auto& input = get_input<InputIndex>();
        
        // Only ContinuousInput needs unsubscription
        using InputWrapper = std::decay_t<decltype(input)>;
        if constexpr (is_continuous_input_v<InputWrapper>) {
            input.unsubscribe();
        }
    }
    
private:
    /**
     * @brief Fetch single input's data
     */
    template<size_t InputIndex>
    void fetch_single_input() {
        auto& input = get_input<InputIndex>();
        
        using InputWrapper = std::decay_t<decltype(input)>;
        
        if constexpr (is_continuous_input_v<InputWrapper>) {
            // ContinuousInput: poll_data() receives into input's buffer
            input.poll_data();
        } else {
            // SyncedInput: get_data() will be called with primary timestamp
            // No action here - handled in call_process
        }
    }
    
    /**
     * @brief Type trait to check if wrapper is ContinuousInput
     */
    template<typename T>
    struct is_continuous_input : std::false_type {};
    
    template<typename R, typename T>
    struct is_continuous_input<ContinuousInput<R, T>> : std::true_type {};
    
    template<typename T>
    static constexpr bool is_continuous_input_v = is_continuous_input<T>::value;
};

} // namespace commrat
