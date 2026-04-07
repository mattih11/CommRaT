#pragma once

/**
 * @file module2.hpp
 * @brief Next-generation Module using I/O tuple architecture
 * 
 * Clean design with:
 * - Compile-time I/O tuple (BuildIOTuple)
 * - Auto-inferred execution mode (Input/Period/Loop)
 * - Integrated command association (DataWithCommands)
 * - Simplified configuration
 * 
 * Threading Architecture (N outputs):
 * - 1 data_thread: Runs process() based on execution mode
 * - N command_threads: One per output, blocking receive on CMD mailbox
 *   - Handles: SubscribeRequest, GetDataRequest, user commands
 * - WorkMailbox: NO dedicated thread (used from main/data thread for sending)
 *   - Subscribe to producers, send control messages, publish outputs
 * 
 * Command Dispatch (per-output CMD mailbox):
 * - Each output has different CMD mailbox address (different type_id)
 * - Two-visitor pattern:
 *   1. System command visitor: Subscribe, GetData (handled by ModuleOutput)
 *   2. User command visitor: Custom commands (dispatched to on_command<> override)
 */

#include "commrat/module/io/io_spec.hpp"
#include "commrat/module/io/output_infrastructure.hpp"
#include "commrat/module/module_config.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/helpers/command_extraction.hpp"
#include "commrat/module/services/io_handler.hpp"
#include "commrat/module/services/command_handler.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include "commrat/messaging/message_registry.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/platform/threading.hpp"
#include "commrat/platform/timestamp.hpp"
#include <chrono>
#include <tuple>
#include <type_traits>

namespace commrat {

// Forward declarations
template<typename Registry, typename T> class ContinuousInput;
template<typename Registry, typename T> class SyncedInputImpl;

// ============================================================================
// Helper: Select appropriate processor base class based on I/O configuration
// ============================================================================

template<typename IOBuilder, typename IOTuple>
struct SelectProcessorBase {
    using IO = typename IOBuilder::Meta;
    
    // Select based on I/O configuration
    using type = std::conditional_t<
        !IO::has_inputs,
        // No inputs: NoInputProcessor<OutputTuple>
        typename NoInputBase<typename IO::InputWrappers, typename IO::OutputTypes>::type,
        // Has inputs (continuous or synced): InputProcessor<InputWrapperTuple, OutputTuple>
        // ProcessParam maps ContinuousInput<T> -> T, SyncedInputImpl<T> -> Synced<T>
        typename InputBase<typename IO::InputWrappers, typename IO::OutputTypes>::type
    >;
};

/**
 * @brief Module using I/O tuple architecture
 * 
 * @tparam Registry Message registry (MessageRegistry<...>)
 * @tparam IOSpecs I/O specifications (Output<T>, Input<T>, Period<D>, SyncedInput<T>)
 * 
 * Inherits appropriate processor base class based on I/O configuration.
 * Process signature auto-determined by ProcessParam trait:
 * - No inputs: void process(O1& out1, ...)
 * - Continuous: void process(const I1& in1, ..., O1& out1, ...)
 * - Synced: void process(const I1& in1, const std::optional<I2>& synced, ..., O1& out1, ...)
 */
template<typename Registry, typename... IOSpecs>
class Module2 
    : public SelectProcessorBase<
        BuildIOTuple<Registry, IOSpecs...>,
        typename BuildIOTuple<Registry, IOSpecs...>::type
    >::type
    , private IOHandler<Registry, IOSpecs...>
    , private CommandHandler<Registry, BuildIOTuple<Registry, IOSpecs...>, typename BuildIOTuple<Registry, IOSpecs...>::type> {
private:
    // ========================================================================
    // Service Aliases
    // ========================================================================
    
    using IOService = IOHandler<Registry, IOSpecs...>;
    using IOBuilder = typename IOService::IOBuilder;
    using IOTuple = typename IOService::IOTuple;
    using CmdService = CommandHandler<Registry, IOBuilder, IOTuple>;
    
    // Expose structured I/O access and operations from IOService
    struct IO : public IOService {
        using IOService::outputs;
        using IOService::inputs;
        using IOService::get_input;
        using IOService::get_output;
        using IOService::get_input_data;
        using IOService::get_output_data;
        using IOService::publish_outputs;
        using IOService::fetch_inputs;
        using IOService::initialize_io;
        using IOService::initialize_outputs;
        using IOService::initialize_inputs;
       using IOService::subscribe_inputs;
        using IOService::unsubscribe_inputs;
        // Alias to IOBuilder::Meta for convenience
        using Meta = typename IOBuilder::Meta;
    };
    
    // Mailbox types
    using WorkMailbox = typename Registry::System::WorkMailbox;
    
    // Threading: 1 data thread + N command threads (one per output)
    Thread data_thread_;                                        // Runs process() and publishes
    std::array<Thread, IOService::num_outputs> command_threads_;   // One per output (CMD mailbox)
    std::atomic<bool> should_stop_{false};
    
    // Mailbox infrastructure (CMD mailboxes owned by ModuleOutput, DATA by ModuleInput)
    std::optional<WorkMailbox> work_mailbox_ = std::nullopt;  // No dedicated thread - sends from main/data thread (default empty)
    

public:
    // ========================================================================
    // Type Aliases (for single I/O modules)
    // ========================================================================
    
    /**
     * @brief Type of the single output (or void if multiple/none)
     * 
     * For modules with exactly one output, this is the payload type.
     * For modules with zero or multiple outputs, this is void.
     * 
     * Example: Module2<Output<SensorData>, ...> -> OutputData = SensorData
     */
    using OutputData = typename IO::Meta::SingleOutputType;
    
    /**
     * @brief Type of the single input (or void if multiple/none)
     * 
     * For modules with exactly one input, this is the payload type.
     * For modules with zero or multiple inputs, this is void.
     * 
     * Example: Module2<..., Input<FilteredData>> -> InputData = FilteredData
     */
    using InputData = typename IO::Meta::SingleInputType;
    // ========================================================================
    // Construction
    // ========================================================================
    
    /**
     * @brief Construct module with configuration
     * 
     * Initialization happens here:
     * - Create WORK mailbox
     * - Initialize outputs (creates CMD mailboxes)
     * - Initialize inputs (creates DATA mailboxes)
     */
    explicit Module2(const ModuleConfig& config)
        : config_(config)
    {
        // Create WORK mailbox for subscription protocol
        create_work_mailbox();
        
        // Initialize I/O instances (delegates to IOService)
        this->initialize_io(config_, *work_mailbox_);
    }
    
    virtual ~Module2() {
        stop();  // Stop data processing
        
        // Full shutdown: join command threads and stop work mailbox
        for (auto& thread : command_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        if (work_mailbox_) {
            work_mailbox_->stop();
        }
    }

protected:

    // Module identification
    ModuleConfig config_;

    /**
     * @brief Get input timestamp by index (convenience method)
     * 
     * @tparam InputIndex The input index (0-based)
     * @return Timestamp from TimsHeader (nanoseconds since epoch)
     */
    template<size_t InputIndex>
        requires (InputIndex < IO::Meta::num_inputs)
    uint64_t get_input_timestamp() const {
        return this->template get_input<InputIndex>().get_timestamp();
    }
    
    /**
     * @brief Check if input has new data by index
     * 
     * Returns true if the input was freshly received and not stale.
     * For synced inputs, returns true if get_data succeeded with fresh data.
     * 
     * @tparam InputIndex The input index (0-based)
     * @return True if fresh data, false if stale
     */
    template<size_t InputIndex>
        requires (InputIndex < IO::Meta::num_inputs)
    bool has_new_data() const {
        return this->template get_input<InputIndex>().is_fresh();
    }
    
    /**
     * @brief Check if input is valid by index
     * 
     * Returns true if receive/get_data succeeded for this input.
     * For synced inputs, returns false if get_data failed.
     * 
     * @tparam InputIndex The input index (0-based)
     * @return True if valid, false if receive/get_data failed
     */
    template<size_t InputIndex>
        requires (InputIndex < IO::Meta::num_inputs)
    bool is_input_valid() const {
        return this->template get_input<InputIndex>().is_valid();
    }

    // ========================================================================
    // Lifecycle
    // ========================================================================
protected:
    virtual void on_start() {}  // Optional override for startup logic
    virtual void on_stop() {}   // Optional override for shutdown logic
public:
    /**
     * @brief Start module execution
     * 
     * Starts subscriptions and launches threads:
     * - Start all mailboxes (WORK, outputs, inputs)
     * - Subscribe inputs to their producers
     * - Launch N command threads (one per output)
     * - Launch data thread (runs process() loop)
     * 
     * Note: Mailboxes created in constructor, activated here
     */
    void start() {
        // Start WORK mailbox (enables subscription protocol)
        start_work_mailbox();
        
        // Start all output mailboxes (CMD + PUBLISH per output)
        this->start_outputs(std::make_index_sequence<IO::Meta::num_outputs>{});
        
        // Start all input mailboxes (DATA for ContinuousInput only)
        this->start_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
        
        // Subscribe all inputs to their producers (delegates to IOService)
        this->subscribe_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
        
        // Start command thread for each output (handles GetData, Subscribe, user commands)
        for (size_t i = 0; i < IO::Meta::num_outputs; ++i) {
            command_threads_[i] = Thread([this, i]() { command_loop(i); });
        }
        
        // Start data thread (runs process() based on execution mode)
        data_thread_ = Thread([this]() { data_loop(); });

        on_start();  // Call user-defined startup logic
    }
    
    /**
     * @brief Stop module execution
     * 
     * Stops data processing loop:
     * - Set stop flag
     * - Unsubscribe all inputs
     * - Join data thread only
     * 
     * Note: Command threads stay active to handle final unsubscribe acknowledgments
     * and any remaining commands. Call destroy() or destructor to fully shut down.
     */
    void stop() {
        on_stop();  // Call user-defined shutdown logic
        should_stop_.store(true, std::memory_order_release);
        
        // Unsubscribe all inputs from their producers (delegates to IOService)
        this->unsubscribe_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
        
        // Stop outputs (CMD and PUBLISH mailboxes)
        this->stop_outputs(std::make_index_sequence<IO::Meta::num_outputs>{});
        
        // Join data thread
        if (data_thread_.joinable()) {
            data_thread_.join();
        }
        
        // Join command threads (one per output)
        for (auto& thread : command_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

private:
    // ========================================================================
    // Internal Initialization
    // ========================================================================
    
    /**
     * @brief Create WORK mailbox (allocation phase - NOT real-time safe)
     * 
     * WORK mailbox address: [type_id][system_id][instance_id][16]
     * Receives: SubscribeReply, UnsubscribeReply
     * Does NOT start mailbox - call start_work_mailbox() separately.
     */
    void create_work_mailbox() {
        // Get module's system_id and instance_id
        // For SimpleOutputConfig: config_.system_id() (no index)
        // For MultiOutputConfig: config_.system_id(0) (first output)
        // For NoOutputConfig: config_.system_id() (module-level)
        uint8_t sys_id, inst_id;
        if (config_.has_multi_output_config()) {
            sys_id = config_.system_id(0);
            inst_id = config_.instance_id(0);
        } else {
            sys_id = config_.system_id();
            inst_id = config_.instance_id();
        }
        
        // Calculate WORK mailbox address using primary output type_id
        // This ensures modules with different output types at the same [sys][inst]
        // get unique WORK mailbox addresses (matching their CMD/PUBLISH mailboxes)
        using PrimaryOutputType = std::conditional_t<
            IO::Meta::has_outputs,
            std::tuple_element_t<0, typename IO::Meta::OutputTypes>,
            void
        >;
        uint32_t work_addr = get_mailbox_address<PrimaryOutputType, std::tuple<>, Registry>(
            sys_id, inst_id, WORK_MBX_BASE
        );
        
        // Create WorkMailbox (allocation only, no start)
        MailboxConfig work_config{
            .mailbox_id = work_addr,
            .message_slots = config_.cmd_message_slots.value(),  // Extract from rfl::DefaultVal
            .max_message_size = Registry::max_message_size,  // Full registry max (unrestricted mailbox)
            .send_priority = static_cast<uint8_t>(config_.priority),
            .realtime = config_.realtime
        };
        
        work_mailbox_.emplace(work_config);
    }
    
    /**
     * @brief Start WORK mailbox (activation phase - real-time safe)
     * 
     * Activates WORK mailbox. Must be called after create_work_mailbox().
     * Real-time safe: no allocations, only activates existing resources.
     * 
     * @throws std::runtime_error if mailbox not created or TiMS start fails
     */
    void start_work_mailbox() {
        if (!work_mailbox_) {
            throw std::runtime_error("WORK mailbox not created - call create_work_mailbox() first");
        }
        
        auto work_result = work_mailbox_->start();
        if (!work_result) {
            throw std::runtime_error("Failed to start WORK mailbox: TiMS initialization failed");
        }
    }
    
    // ========================================================================
    // Thread Loops
    // ========================================================================
    
    /**
     * @brief Command loop for specific output
     * 
     * Handles commands for output at OutputIndex:
     * - System commands: SubscribeRequest, UnsubscribeRequest, GetDataRequest
     * - User commands: Type-specific commands from DataWithCommands
     * 
     * @param output_index Logical output index (0 to IO::Meta::num_outputs-1)
     */
    void command_loop(size_t output_index) {
        // Runtime dispatch to compile-time handler
        dispatch_command_loop(output_index, std::make_index_sequence<IO::Meta::num_outputs>{});
    }
    
    /**
     * @brief Dispatch command loop to specific output handler (compile-time)
     */
    template<size_t... OutputIndices>
    void dispatch_command_loop(size_t output_index, std::index_sequence<OutputIndices...>) {
        // Compile-time switch on output index
        ((OutputIndices == output_index ? command_loop_impl<OutputIndices>() : void()), ...);
    }
    
    /**
     * @brief Command loop implementation for specific output
     * 
     * Each output has its own CMD mailbox (different type_id in address).
     * Receives and dispatches commands using two visitor patterns:
     * 1. System command visitor - SubscribeRequest, GetDataRequest, etc.
     * 2. User command visitor - Custom commands for this output type
     */
    template<size_t OutputIndex>
    void command_loop_impl() {
        auto& output = this->template get_output<OutputIndex>();
        auto& cmd_mailbox = output.get_cmd_mailbox();
        
        while (!should_stop_.load(std::memory_order_acquire)) {
            // Receive command with timeout (allows checking should_stop_ periodically)
            auto result = cmd_mailbox.receive_any_for(
                std::chrono::milliseconds(100),
                [&](auto&& received_msg) {
                    // Try system command visitor first
                    bool handled = visit_system_commands<OutputIndex>(received_msg);
                    
                    if (!handled) {
                        // Fall back to user command visitor
                        visit_user_commands<OutputIndex>(received_msg);
                    }
                }
            );
            
            // result will be timeout if no message received - that's fine,
            // allows us to check should_stop_ flag
        }
    }
    
    /**
     * @brief System command visitor - delegates to CommandHandler service
     * 
     * @return true if message was a system command (handled), false otherwise
     */
    template<size_t OutputIndex, typename ReceivedMsg>
    bool visit_system_commands(ReceivedMsg&& received_msg) {
        auto& output = this->template get_output<OutputIndex>();
        return CmdService::template visit_system_command<OutputIndex>(output, received_msg);
    }
    
    /**
     * @brief User command visitor - delegates to CommandHandler service
     * 
     * @return true if message was a user command (handled), false otherwise
     */
    template<size_t OutputIndex, typename ReceivedMsg>
    bool visit_user_commands(ReceivedMsg&& received_msg) {
        return CmdService::template visit_user_command<OutputIndex>(received_msg, this);
    }
    
    /**
     * @brief Unified data loop - runs process() based on execution mode
     * 
     * Three execution modes:
     * - Input-driven: Block on primary input's poll_data()
     * - Timer-driven: Sleep for period between iterations
     * - Loop-driven: Continuous loop (max throughput)
     */
    void data_loop() {
        Timestamp loop_start = 0;
        
        while (!should_stop_.load(std::memory_order_acquire)) {
            if constexpr (IO::Meta::is_timer_driven) {
                loop_start = Time::now();
            }
            
            // Step 1: Fetch input data (if input-driven, delegates to IOService)
            // Skip process() if primary input had no new data (poll timeout)
            if constexpr (IO::Meta::is_input_driven) {
                bool got_data = this->fetch_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
                if (!got_data) continue;
            }
            
            // Step 2: Call user's process() with unpacked inputs and outputs
            call_process(std::make_index_sequence<IO::Meta::num_inputs>{}, std::make_index_sequence<IO::Meta::num_outputs>{});
            
            // Step 3: Publish outputs (delegates to IOService)
            publish_outputs(std::make_index_sequence<IO::Meta::num_outputs>{});
            
            // Step 4: Sleep if timer-driven (period - processing_time)
            if constexpr (IO::Meta::is_timer_driven) {
                Timestamp now = Time::now();
                Nanoseconds elapsed_ns(now - loop_start);
                auto period = IO::Meta::period;
                auto elapsed_ms = std::chrono::duration_cast<Milliseconds>(elapsed_ns);
                if (elapsed_ms < period) {
                    Time::sleep(period - elapsed_ms);
                }
                // If processing took longer than period, skip sleep and continue immediately
            }
        }
    }
    
private:
    // ========================================================================
    // Data Loop Helpers (delegate to IOService)
    // ========================================================================
    
    /**
     * @brief Call user's process() with unpacked inputs and outputs
     */
    template<size_t... InputIndices, size_t... OutputIndices>
    void call_process(std::index_sequence<InputIndices...>, std::index_sequence<OutputIndices...>) {
        if constexpr (IO::Meta::num_inputs == 0) {
            // No inputs: process(O1& out1, O2& out2, ...)
            this->process(this->template get_output_data<OutputIndices>()...);
        } else {
            // With inputs: process(const ProcessParam_t<I1>& in1, ..., O1& out1, ...)
            this->process(
                this->template get_input_data<InputIndices>()...,
                this->template get_output_data<OutputIndices>()...
            );
        }
    }
    
    /**
     * @brief Publish all outputs - delegates to IOHandler service
     */
    template<size_t... OutputIndices>
    void publish_outputs(std::index_sequence<OutputIndices...>) {
        this->IOService::publish_outputs(std::index_sequence<OutputIndices...>{});
    }
};

} // namespace commrat
