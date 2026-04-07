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
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/traits/type_extraction.hpp"
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
     * @return true if primary input (index 0) received new data, true if no inputs
     */
    template<size_t... InputIndices>
    bool fetch_inputs(std::index_sequence<InputIndices...>) {
        if constexpr (sizeof...(InputIndices) == 0) return true;

        // Step 1: Poll primary input (always index 0, a ContinuousInput)
        bool primary_got_data = get_input<0>().poll_data();
        if (!primary_got_data) return false;

        // Step 2: For each synced input, call get_data with primary timestamp
        if constexpr (num_synced_inputs > 0) {
            Timestamp primary_ts = get_input<0>().get_timestamp();
            (fetch_synced_if_needed<InputIndices>(primary_ts), ...);
        }

        return true;
    }

    /**
     * @brief Call get_data on synced input; no-op for continuous / primary
     */
    template<size_t InputIndex>
    void fetch_synced_if_needed(Timestamp primary_ts) {
        auto& input = get_input<InputIndex>();
        using InputWrapper = std::decay_t<decltype(input)>;
        if constexpr (!is_continuous_input_v<InputWrapper> && InputIndex != 0) {
            using SyncedT = decltype(input.get_payload());  // Synced<T>
            using DataT   = typename SyncedT::value_type;
            const TimsMessage<DataT>* msg_ptr = nullptr;
            input.get_data(primary_ts, msg_ptr);
        }
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
        // CRITICAL: Emplace IOTuple BEFORE accessing get_output()/get_input()!
        // get_output() dereferences io_instances_, so it MUST be initialized first.
        io_instances_.emplace();
        
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
     * @brief Start all outputs (real-time safe activation)
     */
    template<size_t... OutputIndices>
    void start_outputs(std::index_sequence<OutputIndices...>) {
        (start_output<OutputIndices>(), ...);
    }
    
    /**
     * @brief Start single output at index (real-time safe)
     */
    template<size_t OutputIndex>
    void start_output() {
        auto& output = get_output<OutputIndex>();
        output.start();
    }
    
    /**
     * @brief Stop all outputs (real-time safe deactivation)
     */
    template<size_t... OutputIndices>
    void stop_outputs(std::index_sequence<OutputIndices...>) {
        (stop_output<OutputIndices>(), ...);
    }
    
    /**
     * @brief Stop single output at index (real-time safe)
     */
    template<size_t OutputIndex>
    void stop_output() {
        auto& output = get_output<OutputIndex>();
        output.stop();
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
        using InputType = std::decay_t<decltype(input)>;
        
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
        
        // Handle initialization based on input type
        if constexpr (is_continuous_input_v<InputType>) {
            // ContinuousInput: Create DATA mailbox for streaming
            using DataType = typename InputType::DataMessage::Payload;
            
            // Calculate DATA mailbox address for consumer (this module)
            // DATA mailboxes start at index 3 (after CMD=0, WORK=1, PUBLISH=2)
            // Use output 0's address for multi-output modules
            uint8_t self_sys_id = config.has_multi_output_config() ? config.system_id(0) : config.system_id();
            uint8_t self_inst_id = config.has_multi_output_config() ? config.instance_id(0) : config.instance_id();
            uint32_t data_addr = get_mailbox_address<DataType, std::tuple<>, Registry>(
                self_sys_id,
                self_inst_id,
                DATA_MBX_BASE + static_cast<uint8_t>(InputIndex)
            );
            
            // Create DATA mailbox config
            MailboxConfig data_config{
                .mailbox_id = data_addr,
                .message_slots = 10,  // Data stream buffer
                .max_message_size = get_data_mailbox_size<DataType>(),
                .send_priority = 50,
                .realtime = true
            };
            
            // Initialize ContinuousInput with DATA mailbox
            input.initialize(
                work_mailbox,
                data_config,
                src_sys_id,
                src_inst_id,
                Milliseconds::zero(),   // Requested period
                Milliseconds(100),      // Poll timeout
                Milliseconds(1000)      // Command timeout
            );
        } else {
            // SyncedInput: NO DATA mailbox, only work_mbx reference
            input.initialize(
                work_mailbox,
                src_sys_id,
                src_inst_id,
                config.sync_tolerance(),
                InterpolationMode::NEAREST,
                Milliseconds(1000)      // Command timeout
            );
        }
    }
    
    // ========================================================================
    // Input Lifecycle (Start/Stop)
    // ========================================================================
    
    /**
     * @brief Start all input mailboxes (only ContinuousInput has mailboxes)
     */
    template<size_t... InputIndices>
    void start_inputs(std::index_sequence<InputIndices...>) {
        (start_input<InputIndices>(), ...);
    }
    
    /**
     * @brief Start single input at index (no-op for SyncedInput)
     */
    template<size_t InputIndex>
    void start_input() {
        auto& input = get_input<InputIndex>();
        using InputType = std::decay_t<decltype(input)>;
        
        // Only ContinuousInput has mailboxes to start
        if constexpr (is_continuous_input_v<InputType>) {
            input.start();
        }
        // SyncedInput has no mailboxes - no-op
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
    bool fetch_single_input() {
        auto& input = get_input<InputIndex>();
        
        using InputWrapper = std::decay_t<decltype(input)>;
        
        if constexpr (is_continuous_input_v<InputWrapper>) {
            // ContinuousInput: poll_data() receives into input's buffer
            // Returns true if new data received, false on timeout
            return input.poll_data();
        } else {
            // SyncedInput: get_data() will be called with primary timestamp
            // No action here - handled in call_process
            return true;
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
