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
 * - 1 data_thread: Runs lifecycle transitions and process()
 * - 1 lifecycle_thread: Handles module-level on/off/status commands
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
#include "commrat/module/params.hpp"
#include <sertial/containers/reflectors.hpp>
#include <rfl/json.hpp>
#include <fstream>
#include "commrat/messaging/system/param_messages.hpp"
#include "commrat/module/helpers/command_extraction.hpp"
#include "commrat/module/services/io_handler.hpp"
#include "commrat/module/services/command_handler.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include "commrat/messaging/message_registry.hpp"
#include "commrat/messaging/system/lifecycle_messages.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include <corerat/platform/threading.hpp>
#include <corerat/platform/timestamp.hpp>
#include <corerat/platform/duration.hpp>
#include <corerat/platform/platform.hpp>
#include <corerat/logging/logging.hpp>
#include <tuple>
#include <type_traits>
#include <iostream>

namespace commrat {

using corerat::Thread;
using corerat::Duration;
using corerat::Milliseconds;
using corerat::Timestamp;
using corerat::Time;

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

    // Extract Params<T> type from IOSpecs (void if not specified).
    // Uses a helper struct to avoid instantiating Head::Type when Head is not Params<T>.
    template<typename Head, bool IsParams>
    struct ParamsTypeOf { using type = void; };
    template<typename Head>
    struct ParamsTypeOf<Head, true> { using type = typename Head::Type; };

    template<typename... Specs>
    struct ExtractParams { using type = void; };
    template<typename Head, typename... Tail>
    struct ExtractParams<Head, Tail...> {
        using type = std::conditional_t<
            is_params_spec_v<Head>,
            typename ParamsTypeOf<Head, is_params_spec_v<Head>>::type,
            typename ExtractParams<Tail...>::type>;
    };
public:
    /// Compile-time I/O topology — used by write_module_inspect() for --commrat-inspect.
    using IOBuilder = BuildIOTuple<Registry, IOSpecs...>;

    /// Message registry used to resolve output-specific command associations.
    using RegistryType = Registry;

    /// Params type extracted from Params<T> IOSpec; void when no Params<T> given.
    /// Introspection hook: meta/inspect.hpp should check ModuleType::ParamsType.
    using ParamsType = typename ExtractParams<IOSpecs...>::type;
    static constexpr bool has_params = !std::is_same_v<ParamsType, void>;

private:
    // ========================================================================
    // Service Aliases
    // ========================================================================
    
    using IOService = IOHandler<Registry, IOSpecs...>;
    using IOTuple = typename IOService::IOTuple;
    using CmdService = CommandHandler<Registry, IOBuilder, IOTuple>;
    
    // Expose structured I/O access and operations from IOService
    struct IO : public IOService {
        using IOService::outputs;
        using IOService::inputs;
        using IOService::get_input;
        using IOService::get_remote;
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
    using LifecycleMailbox = CommandMailbox<
        Registry,
        LifecycleOnPayload,
        LifecycleOnReplyPayload,
        LifecycleOffPayload,
        LifecycleOffReplyPayload,
        GetLifecycleStatusPayload,
        LifecycleStatusReplyPayload>;
    
    // Threading: 1 data thread + 1 lifecycle thread + N output command threads
    Thread data_thread_;                                        // Runs process() and publishes
    Thread lifecycle_thread_;                                   // Module-level on/off/status commands
    std::array<Thread, IOService::num_outputs> command_threads_;   // One per output (CMD mailbox)
    std::atomic<bool> should_stop_{false};
    enum class RuntimeState : uint8_t {
        Constructed,
        Started,
        Stopped
    };

    std::atomic<RuntimeState> runtime_state_{RuntimeState::Constructed};
    std::atomic<LifecycleState> lifecycle_state_{LifecycleState::Disabled};
    std::atomic<LifecycleTarget> lifecycle_target_{LifecycleTarget::Off};
    std::atomic<uint32_t> lifecycle_error_code_{0};
    std::atomic<uint64_t> lifecycle_state_since_ns_{0};

    enum class PendingLifecycleRequest : uint8_t {
        None,
        Writing,
        OnRequested,
        OffRequested,
        OnCompleted,
        OffCompleted
    };

    std::atomic<PendingLifecycleRequest> pending_lifecycle_request_{
        PendingLifecycleRequest::None};
    TimsHeader pending_lifecycle_header_{};
    
    // Mailbox infrastructure (CMD mailboxes owned by ModuleOutput, DATA by ModuleInput)
    std::optional<WorkMailbox> work_mailbox_ = std::nullopt;  // No dedicated thread - sends from main/data thread (default empty)
    std::optional<RpcClient<Registry>> rpc_client_ = std::nullopt;
    std::optional<LifecycleMailbox> lifecycle_mailbox_ = std::nullopt;

    static constexpr size_t max_registered_command_handlers = 32;

    struct RegisteredCommandHandler {
        size_t output_index;
        uint32_t msg_type;
        void* object;
        void (*invoke)(void*, const void*, void*);
    };

    std::array<RegisteredCommandHandler, max_registered_command_handlers> command_handlers_{};
    size_t command_handler_count_{0};

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

    template<typename OutputDataType, typename CmdType>
    std::optional<TimsMessage<typename CmdType::Reply>> send_command(
        uint8_t target_system_id,
        uint8_t target_instance_id,
        const CmdType& command,
        Duration timeout = Milliseconds(100)) {
        static_assert(is_in_tuple_v<
                          CmdType,
                          registry::get_commands_for_t<OutputDataType, Registry>>,
                      "Command type is not associated with the target output type");

        RemoteHandle<Registry, OutputDataType> target;
        target.initialize(
            *rpc_client_, target_system_id, target_instance_id, timeout);
        return target.template send_command<CmdType>(command, timeout);
    }

    template<size_t InputIndex, typename CmdType>
        requires (InputIndex < IO::Meta::num_inputs)
    std::optional<TimsMessage<typename CmdType::Reply>> send_command_to_input(
        const CmdType& command,
        Duration timeout = Milliseconds(100)) {
        return this->template get_input<InputIndex>()
            .template send_command<CmdType>(command, timeout);
    }

    template<size_t OutputIndex>
        requires (OutputIndex < IO::Meta::num_outputs)
    [[nodiscard]] uint32_t output_command_address() const {
        return this->template get_output<OutputIndex>().get_cmd_address();
    }

    template<typename TargetOutput = void>
    [[nodiscard]] static constexpr uint32_t lifecycle_command_address(
        uint8_t system_id,
        uint8_t instance_id) {
        return get_lifecycle_address<TargetOutput, Registry>(system_id, instance_id);
    }

    [[nodiscard]] LifecycleState lifecycle_state() const {
        return lifecycle_state_.load(std::memory_order_acquire);
    }

    [[nodiscard]] LifecycleTarget lifecycle_target() const {
        return lifecycle_target_.load(std::memory_order_acquire);
    }

    template<typename TargetOutput = void>
    std::optional<TimsMessage<LifecycleOnReplyPayload>> lifecycle_on(
        uint8_t target_system_id,
        uint8_t target_instance_id,
        Duration timeout = Milliseconds(100)) {
        return send_lifecycle_rpc<TargetOutput, LifecycleOnPayload, LifecycleOnReplyPayload>(
            target_system_id, target_instance_id, LifecycleOnPayload{}, timeout);
    }

    template<typename TargetOutput = void>
    std::optional<TimsMessage<LifecycleOffReplyPayload>> lifecycle_off(
        uint8_t target_system_id,
        uint8_t target_instance_id,
        Duration timeout = Milliseconds(100)) {
        return send_lifecycle_rpc<TargetOutput, LifecycleOffPayload, LifecycleOffReplyPayload>(
            target_system_id, target_instance_id, LifecycleOffPayload{}, timeout);
    }

    template<typename TargetOutput = void>
    std::optional<TimsMessage<LifecycleStatusReplyPayload>> get_lifecycle_status(
        uint8_t target_system_id,
        uint8_t target_instance_id,
        Duration timeout = Milliseconds(100)) {
        return send_lifecycle_rpc<TargetOutput, GetLifecycleStatusPayload, LifecycleStatusReplyPayload>(
            target_system_id, target_instance_id, GetLifecycleStatusPayload{}, timeout);
    }

    template<size_t OutputIndex, typename CmdType, auto Handler, typename ModuleType>
        requires (OutputIndex < IO::Meta::num_outputs)
    bool register_command_handler(ModuleType& module) {
        using OutputType = std::tuple_element_t<OutputIndex, typename IO::Meta::OutputTypes>;
        using Commands = registry::get_commands_for_t<OutputType, Registry>;
        static_assert(is_in_tuple_v<CmdType, Commands>,
                      "Command type is not associated with this output");
        static_assert(std::is_invocable_r_v<
                          void, decltype(Handler), ModuleType*,
                          const CmdType&, typename CmdType::Reply&>,
                      "Command handler must accept (const CmdType&, CmdType::Reply&)");

        if (command_handler_count_ >= command_handlers_.size()) {
            return false;
        }

        command_handlers_[command_handler_count_++] = RegisteredCommandHandler{
            .output_index = OutputIndex,
            .msg_type = Registry::template get_message_id<CmdType>(),
            .object = &module,
            .invoke = [](void* object, const void* command, void* reply) {
                (static_cast<ModuleType*>(object)->*Handler)(
                    *static_cast<const CmdType*>(command),
                    *static_cast<typename CmdType::Reply*>(reply));
            }
        };
        return true;
    }

    template<size_t OutputIndex, typename ReceivedMsg, typename CmdMailboxType>
    bool dispatch_registered_command(
        const ReceivedMsg& received_msg,
        CmdMailboxType& cmd_mailbox) {
        const uint32_t msg_type = received_msg.header.msg_type;

        for (size_t index = 0; index < command_handler_count_; ++index) {
            const auto& handler = command_handlers_[index];
            if (handler.output_index != OutputIndex || handler.msg_type != msg_type) {
                continue;
            }

            typename ReceivedMsg::payload_type::Reply reply{};
            handler.invoke(handler.object, &received_msg.payload, &reply);
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        return false;
    }

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
        , logger_(compute_work_addr(config))
    {
        if constexpr (has_params) {
            params_ = params_from_config<ParamsType>(config);
        }
        // Print platform info once per process
        [[maybe_unused]] static bool platform_printed = []() {
#if defined(CORERAT_PLATFORM_EVL)
            std::clog << "[CommRaT] Platform: EVL (Xenomai 4 / libevl)"
                      << " | OOB=" << CORERAT_HAS_OOB
                      << " | PI-mutex=" << CORERAT_HAS_PI_MUTEX
                      << "\n";
#else
            std::clog << "[CommRaT] Platform: STD (standard Linux)\n";
#endif
            return true;
        }();
        // Wire up the terminal sink (done once; add_sink stores a pointer)
        logger_.add_sink(&terminal_sink_);

        // Create WORK mailbox for subscription protocol
        create_work_mailbox();
        rpc_client_.emplace(*work_mailbox_);
        create_lifecycle_mailbox();
        
        // Initialize I/O instances (delegates to IOService)
        this->initialize_io(config_, *rpc_client_);
    }
    
    virtual ~Module2() {
        stop();
    }

protected:

    // Module identification
    ModuleConfig config_;

    // params_ is only present when Params<T> is in the IOSpecs.
    [[no_unique_address]] std::conditional_t<has_params, ParamsType, std::monostate> params_{};
    // Guards params_: data thread holds shared lock; command thread holds exclusive lock.
    [[no_unique_address]] std::conditional_t<has_params, SharedMutex, std::monostate> params_mutex_{};

    // Logging — TerminalSink declared before RtLogger so it outlives the drain thread.
    corerat::TerminalSink terminal_sink_;
    corerat::RtLogger<>   logger_;

    template<size_t InputIndex>
        requires (InputIndex < IO::Meta::num_inputs)
    auto& input() {
        return this->template get_input<InputIndex>();
    }

    template<typename InputType>
    auto& input() {
        static_assert(
            tuple_type_count_v<InputType, typename IO::Meta::InputTypes> == 1,
            "input<T>() requires T to occur exactly once; use input<Index>()");
        constexpr size_t index = tuple_type_index_v<
            InputType, typename IO::Meta::InputTypes>;
        return this->template get_input<index>();
    }

    template<size_t RemoteIndex>
        requires (RemoteIndex < IO::Meta::num_remotes)
    auto& remote() {
        return this->template get_remote<RemoteIndex>();
    }

    template<typename RemoteType>
    auto& remote() {
        static_assert(
            tuple_type_count_v<RemoteType, typename IO::Meta::RemoteTypes> == 1,
            "remote<T>() requires T to occur exactly once; use remote<Index>()");
        constexpr size_t index = tuple_type_index_v<
            RemoteType, typename IO::Meta::RemoteTypes>;
        return this->template get_remote<index>();
    }

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
    virtual LifecycleResult on_enable() { return LifecycleResult::Success; }
    virtual void on_disable() {}

    // ---- Parameter interface — auto-implemented when Params<T> is in IOSpecs ----
    virtual void on_params_changed() {}

    virtual bool commrat_has_params_() const final { return has_params; }

    virtual std::string commrat_get_params_json_() const final {
        if constexpr (has_params)
            return rfl::json::write(params_);
        return "{}";
    }

    virtual bool commrat_set_params_json_(const std::string& json) final {
        if constexpr (has_params) {
            auto result = rfl::json::read<ParamsType, rfl::DefaultIfMissing>(json);
            if (!result) return false;
            {
                UniqueLockShared lk(params_mutex_);
                params_ = result.value();
            }
            on_params_changed();
            return true;
        }
        return false;
    }
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
        auto expected = RuntimeState::Constructed;
        if (!runtime_state_.compare_exchange_strong(
                expected, RuntimeState::Started, std::memory_order_acq_rel)) {
            if (expected == RuntimeState::Stopped) {
                throw std::logic_error("Module2 runtime cannot restart after stop()");
            }
            return;
        }

        should_stop_.store(false, std::memory_order_release);
#define _MSTEP(msg) do { ::fprintf(stderr, "[start:%s] %s\n", config_.name.c_str(), msg); } while(0)
        _MSTEP("drain");
        // Start the in-band log drain thread before any RT thread is launched.
        logger_.start_drain();

        _MSTEP("work_mailbox");
        // Start WORK mailbox (enables subscription protocol)
        start_work_mailbox();
        
        _MSTEP("outputs");
        // Start all output mailboxes (CMD + PUBLISH per output)
        this->start_outputs(std::make_index_sequence<IO::Meta::num_outputs>{});

        _MSTEP("lifecycle_mailbox");
        start_lifecycle_mailbox();
        
        _MSTEP("inputs");
        // Start all input mailboxes (DATA for ContinuousInput only)
        this->start_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
        
        _MSTEP("on_start");
        on_start();

        _MSTEP("enable");
        lifecycle_target_.store(LifecycleTarget::On, std::memory_order_release);
        set_lifecycle_state(LifecycleState::Enabling);
        const LifecycleResult enable_result = invoke_on_enable();

        _MSTEP("subscribe");
        // Subscribe all inputs to their producers (delegates to IOService)
        if (enable_result == LifecycleResult::Success) {
            this->subscribe_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
            lifecycle_error_code_.store(0, std::memory_order_release);
            set_lifecycle_state(LifecycleState::Enabled);
        } else {
            lifecycle_error_code_.store(1, std::memory_order_release);
            set_lifecycle_state(LifecycleState::Error);
        }
        
        _MSTEP("cmd_threads");
        // Start command threads after all mailboxes are ready.
        // Use compile-time index expansion so we can form the EVL thread name
        // from the per-output CMD mailbox address (requires template get_output<N>()).
        start_command_threads(std::make_index_sequence<IO::Meta::num_outputs>{});

        _MSTEP("lifecycle_thread");
        {
            char lifecycle_tname[64];
            std::snprintf(lifecycle_tname, sizeof(lifecycle_tname), "%s-lifecycle/%08X",
                          config_.name.c_str(), lifecycle_mailbox_->mailbox_id());
            lifecycle_thread_.start(ThreadConfig{.name = lifecycle_tname},
                                    [this]() { lifecycle_loop(); });
        }
        
        _MSTEP("data_thread");
        // Start data thread (runs process() based on execution mode)
        {
            char data_tname[64];
            std::snprintf(data_tname, sizeof(data_tname), "%s-data/%08X",
                          config_.name.c_str(),
                          compute_work_addr(config_));
            data_thread_.start(ThreadConfig{.name = data_tname},
                               [this]() { data_loop(); });
        }

        _MSTEP("done");
#undef _MSTEP
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
        auto expected = RuntimeState::Started;
        if (!runtime_state_.compare_exchange_strong(
                expected, RuntimeState::Stopped, std::memory_order_acq_rel)) {
            return;
        }

        should_stop_.store(true, std::memory_order_release);

        // Join data thread
        if (data_thread_.joinable()) {
            data_thread_.join();
        }

        if (lifecycle_state_.load(std::memory_order_acquire) == LifecycleState::Enabled) {
            this->unsubscribe_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
            invoke_on_disable();
        }
        lifecycle_target_.store(LifecycleTarget::Off, std::memory_order_release);
        set_lifecycle_state(LifecycleState::Disabled);

        on_stop();

        this->stop_outputs(std::make_index_sequence<IO::Meta::num_outputs>{});
        if (lifecycle_mailbox_) {
            lifecycle_mailbox_->stop();
        }
        
        // Join command threads (one per output)
        for (auto& thread : command_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        if (lifecycle_thread_.joinable()) {
            lifecycle_thread_.join();
        }

        if (work_mailbox_) {
            work_mailbox_->stop();
        }

        // Flush all buffered log entries now that all RT threads have exited.
        logger_.stop_drain();
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
    /// Compute the WORK mailbox address for this module instance.
    ///
    /// Called both in the constructor initializer list (to seed the logger source_id)
    /// and inside create_work_mailbox().  Must be a static method so it can be used
    /// before the object is fully constructed.
    static uint32_t compute_work_addr(const ModuleConfig& config) {
        uint8_t sys_id, inst_id;
        if (config.has_multi_output_config()) {
            sys_id = config.system_id(0);
            inst_id = config.instance_id(0);
        } else {
            sys_id = config.system_id();
            inst_id = config.instance_id();
        }
        // Note: std::conditional_t evaluates both branches eagerly, so we guard
        // std::tuple_element_t with a non-empty fallback tuple to avoid a hard
        // error when OutputTypes is empty (input-only modules).
        using SafeOutputTypes = std::conditional_t<
            IO::Meta::has_outputs,
            typename IO::Meta::OutputTypes,
            std::tuple<void>
        >;
        using PrimaryOutputType = std::conditional_t<
            IO::Meta::has_outputs,
            std::tuple_element_t<0, SafeOutputTypes>,
            void
        >;
        return get_mailbox_address<PrimaryOutputType, std::tuple<>, Registry>(
            sys_id, inst_id, WORK_MBX_BASE
        );
    }

    void create_work_mailbox() {
        uint32_t work_addr = compute_work_addr(config_);
        
        // Create WorkMailbox (allocation only, no start)
        MailboxConfig work_config{
            .mailbox_id = work_addr,
            .message_slots = config_.cmd_message_slots.value(),  // Extract from rfl::DefaultVal
            .max_message_size = Registry::max_reply_message_size,
            .send_priority = static_cast<uint8_t>(config_.priority),
            .realtime = config_.realtime
        };
        
        work_mailbox_.emplace(work_config);
    }

    void create_lifecycle_mailbox() {
        const uint8_t system_id = config_.has_multi_output_config()
            ? config_.system_id(0)
            : config_.system_id();
        const uint8_t instance_id = config_.has_multi_output_config()
            ? config_.instance_id(0)
            : config_.instance_id();

        using SafeOutputTypes = std::conditional_t<
            IO::Meta::has_outputs,
            typename IO::Meta::OutputTypes,
            std::tuple<void>>;
        using PrimaryOutputType = std::conditional_t<
            IO::Meta::has_outputs,
            std::tuple_element_t<0, SafeOutputTypes>,
            void>;

        MailboxConfig lifecycle_config{
            .mailbox_id = get_lifecycle_address<PrimaryOutputType, Registry>(
                system_id, instance_id),
            .message_slots = config_.cmd_message_slots.value(),
            .max_message_size = LifecycleMailbox::max_message_size,
            .send_priority = static_cast<uint8_t>(config_.priority),
            .realtime = config_.realtime
        };
        lifecycle_mailbox_.emplace(lifecycle_config);
    }

    void start_lifecycle_mailbox() {
        auto result = lifecycle_mailbox_->start();
        if (!result) {
            throw std::runtime_error("Failed to start lifecycle mailbox");
        }
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
            cmd_mailbox.receive_any_for(
                Milliseconds(100),
                [&](auto&& received_msg) {
                    // Try system command visitor first
                    bool handled = visit_system_commands<OutputIndex>(received_msg);
                    
                    if (!handled) handled = visit_param_commands<OutputIndex>(received_msg);

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
        auto& output = this->template get_output<OutputIndex>();
        return CmdService::template visit_user_command<OutputIndex>(output, received_msg, this);
    }

    // Handle GetParams / SetParams commands automatically for any module
    // that overrides commrat_has_params_() / commrat_get_params_json_() / commrat_set_params_json_().
    template<size_t OutputIndex, typename ReceivedMsg>
    bool visit_param_commands(ReceivedMsg&& received_msg) {
        using CmdType = typename std::decay_t<decltype(received_msg)>::payload_type;
        auto& output = this->template get_output<OutputIndex>();
        auto& cmd_mailbox = output.get_cmd_mailbox();

        if constexpr (std::is_same_v<CmdType, GetParamsPayload>) {
            GetParamsReplyPayload reply{};
            reply.has_params = commrat_has_params_();
            if (reply.has_params) {
                auto json = commrat_get_params_json_();
                reply.json = sertial::fixed_string<512>(json.substr(0, 511));
            }
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        if constexpr (std::is_same_v<CmdType, SetParamsPayload>) {
            SetParamsReplyPayload reply{};
            std::string json(received_msg.payload.json.data());
            reply.success = commrat_set_params_json_(json);
            if (!reply.success)
                reply.error = sertial::fixed_string<128>("set_params failed");
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        if constexpr (std::is_same_v<CmdType, ListParamsPayload>) {
            ListParamsReplyPayload reply{};
            reply.has_params = has_params;
            if constexpr (has_params) {
                SharedLock lk(params_mutex_);
                rfl::to_named_tuple(params_).apply([&](const auto&... fields) {
                    ([&](const auto& field) {
                        using FieldType = std::decay_t<decltype(field.value())>;
                        ParamInfo info;
                        auto name_sv = field.name();
                        info.name = sertial::fixed_string<48>(std::string(name_sv).c_str());
                        info.type_name = sertial::fixed_string<48>(rfl::type_name_t<FieldType>().str().c_str());
                        auto val = rfl::json::write(field.value());
                        info.value_json = sertial::fixed_string<128>(val.substr(0, 127).c_str());
                        reply.params.push_back(info);
                    }(fields), ...);
                });
            }
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        if constexpr (std::is_same_v<CmdType, GetParamPayload>) {
            GetParamReplyPayload reply{};
            if constexpr (has_params) {
                std::string_view want(received_msg.payload.name.data());
                SharedLock lk(params_mutex_);
                rfl::to_named_tuple(params_).apply([&](const auto&... fields) {
                    ([&](const auto& field) {
                        if (!reply.found && field.name() == want) {
                            using FieldType = std::decay_t<decltype(field.value())>;
                            reply.name      = sertial::fixed_string<48>(std::string(want).c_str());
                            reply.type_name = sertial::fixed_string<48>(rfl::type_name_t<FieldType>().str().c_str());
                            auto val        = rfl::json::write(field.value());
                            reply.value_json = sertial::fixed_string<256>(val.substr(0, 255).c_str());
                            reply.found = true;
                        }
                    }(fields), ...);
                });
            }
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        if constexpr (std::is_same_v<CmdType, SetParamPayload>) {
            SetParamReplyPayload reply{};
            if constexpr (has_params) {
                std::string_view name(received_msg.payload.name.data());
                std::string_view value(received_msg.payload.value_json.data());
                // Merge: serialize current params to Generic, update one key, parse back.
                auto maybe_obj = rfl::json::read<rfl::Generic>(rfl::json::write(params_));
                auto maybe_val = rfl::json::read<rfl::Generic>(std::string(value));
                if (maybe_obj && maybe_val) {
                    auto* obj_ptr = std::get_if<rfl::Generic::Object>(&maybe_obj.value().variant());
                    if (obj_ptr) {
                        (*obj_ptr)[std::string(name)] = maybe_val.value();
                        auto merged = rfl::json::write(maybe_obj.value());
                        auto result = rfl::json::read<ParamsType>(merged);
                        if (result) {
                            UniqueLockShared lk(params_mutex_);
                            params_ = result.value();
                            reply.success = true;
                        } else {
                            reply.error = sertial::fixed_string<128>("type mismatch or invalid value");
                        }
                    } else {
                        reply.error = sertial::fixed_string<128>("params is not a JSON object");
                    }
                } else {
                    reply.error = sertial::fixed_string<128>("invalid JSON value");
                }
                if (reply.success) on_params_changed();
            } else {
                reply.error = sertial::fixed_string<128>("module has no Params");
            }
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        if constexpr (std::is_same_v<CmdType, SaveParamsPayload>) {
            SaveParamsReplyPayload reply{};
            if constexpr (has_params) {
                std::string path(received_msg.payload.path.data());
                if (path.empty()) {
                    path = "/tmp/commrat_params_" + std::to_string(
                        static_cast<int>(config_.system_id())) + "_" +
                        std::to_string(static_cast<int>(config_.instance_id())) + ".json";
                }
                // TODO(EVL): std::ofstream demotes the command thread to in-band.
                // Replace with corerat::RtFile once CoreRaT EVL IPC backend lands.
                try {
                    SharedLock lk(params_mutex_);
                    std::ofstream f(path);
                    if (f) {
                        f << rfl::json::write(params_) << '\n';
                        reply.success = true;
                    } else {
                        reply.error = sertial::fixed_string<128>("cannot open file");
                    }
                } catch (...) {
                    reply.error = sertial::fixed_string<128>("write error");
                }
            } else {
                reply.error = sertial::fixed_string<128>("module has no Params");
            }
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        if constexpr (std::is_same_v<CmdType, LoadParamsPayload>) {
            LoadParamsReplyPayload reply{};
            if constexpr (has_params) {
                std::string path(received_msg.payload.path.data());
                // TODO(EVL): rfl::json::load uses std::ifstream — demotes command thread to in-band.
                // Replace with corerat::RtFile once CoreRaT EVL IPC backend lands.
                auto result = rfl::json::load<ParamsType>(path);
                if (result) {
                    {
                        UniqueLockShared lk(params_mutex_);
                        params_ = result.value();
                    }
                    on_params_changed();
                    reply.success = true;
                } else {
                    reply.error = sertial::fixed_string<128>("load failed");
                }
            } else {
                reply.error = sertial::fixed_string<128>("module has no Params");
            }
            cmd_mailbox.send_reply(received_msg, reply);
            return true;
        }

        return false;
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
            service_lifecycle_transition();
            if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Enabled) {
                Time::sleep(Milliseconds(10));
                continue;
            }

            if constexpr (IO::Meta::is_timer_driven) {
                loop_start = Time::now();
            }
            
            // Step 1: Fetch input data (if input-driven, delegates to IOService)
            // Skip process() if primary input had no new data (poll timeout)
            if constexpr (IO::Meta::is_input_driven) {
                bool got_data = this->fetch_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
                if (!got_data) {
                    RTLOG_DEBUG(logger_) << "[data_loop] input poll timeout, skipping";
                    continue;
                }
            }
            
            // Step 2: Call user's process() with unpacked inputs and outputs
            // Hold shared lock so the command thread cannot write params_ concurrently.
            if constexpr (has_params) {
                SharedLock lk(params_mutex_);
                call_process(std::make_index_sequence<IO::Meta::num_inputs>{}, std::make_index_sequence<IO::Meta::num_outputs>{});
            } else {
                call_process(std::make_index_sequence<IO::Meta::num_inputs>{}, std::make_index_sequence<IO::Meta::num_outputs>{});
            }
            
            // Step 3: Publish outputs (delegates to IOService)
            publish_outputs(std::make_index_sequence<IO::Meta::num_outputs>{});
            
            // Step 4: Sleep if timer-driven (period - processing_time);
            // yield if loop-driven so other threads (including stop() caller) get CPU.
            if constexpr (IO::Meta::is_timer_driven) {
                Timestamp now = Time::now();
                Duration elapsed = Duration::nanoseconds(static_cast<int64_t>(now - loop_start));
                auto period = IO::Meta::period;
                if (elapsed < period) {
                    Time::sleep(period - elapsed);
                } else {
                    RTLOG_WARN(logger_) << "[data_loop] period overrun: elapsed="
                                       << elapsed.count_ns() / 1'000'000
                                       << "ms period=" << period.count_ns() / 1'000'000 << "ms";
                }
            } else if constexpr (IO::Meta::is_loop_driven) {
                // Loop-driven: no sleep, but yield so other threads can run.
                // This allows stop() to set should_stop_ and the main thread
                // to remain responsive. The user's process() may itself block
                // (e.g. a blocking read() call), in which case this yield is
                // effectively free.
                Time::yield();
            }
        }
    }
    
private:
    template<typename TargetOutput, typename RequestPayload, typename ReplyPayload>
    std::optional<TimsMessage<ReplyPayload>> send_lifecycle_rpc(
        uint8_t target_system_id,
        uint8_t target_instance_id,
        const RequestPayload& payload,
        Duration timeout) {
        const uint32_t target_address = get_lifecycle_address<TargetOutput, Registry>(
            target_system_id, target_instance_id);
        TimsMessage<RequestPayload> request{
            .header = {
                .msg_type = Registry::template get_message_id<RequestPayload>(),
                .msg_size = 0,
                .timestamp = Time::now(),
                .seq_number = 0,
                .dest = target_address,
                .src = work_mailbox_->mailbox_id(),
                .flags = 0
            },
            .payload = payload
        };

        TimsMessage<ReplyPayload> reply;
        if (rpc_client_->transact(request, reply, target_address, timeout)) {
            return reply;
        }
        return std::nullopt;
    }

    void set_lifecycle_state(LifecycleState state) {
        lifecycle_state_since_ns_.store(Time::now(), std::memory_order_release);
        lifecycle_state_.store(state, std::memory_order_release);
    }

    void service_lifecycle_transition() {
        const auto request = pending_lifecycle_request_.load(std::memory_order_acquire);
        if (request == PendingLifecycleRequest::OffRequested) {
            set_lifecycle_state(LifecycleState::Disabling);
            this->unsubscribe_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
            invoke_on_disable();
            lifecycle_error_code_.store(0, std::memory_order_release);
            set_lifecycle_state(LifecycleState::Disabled);
            pending_lifecycle_request_.store(
                PendingLifecycleRequest::OffCompleted, std::memory_order_release);
        } else if (request == PendingLifecycleRequest::OnRequested) {
            set_lifecycle_state(LifecycleState::Enabling);
            const LifecycleResult result = invoke_on_enable();
            if (result == LifecycleResult::Success) {
                this->subscribe_inputs(std::make_index_sequence<IO::Meta::num_inputs>{});
                lifecycle_error_code_.store(0, std::memory_order_release);
                set_lifecycle_state(LifecycleState::Enabled);
            } else {
                lifecycle_error_code_.store(1, std::memory_order_release);
                set_lifecycle_state(LifecycleState::Error);
            }
            pending_lifecycle_request_.store(
                PendingLifecycleRequest::OnCompleted, std::memory_order_release);
        }
    }

    LifecycleResult invoke_on_enable() {
        if constexpr (has_params) {
            SharedLock lock(params_mutex_);
            return on_enable();
        }
        return on_enable();
    }

    void invoke_on_disable() {
        if constexpr (has_params) {
            SharedLock lock(params_mutex_);
            on_disable();
        } else {
            on_disable();
        }
    }

    template<typename ReplyPayload>
    ReplyPayload make_lifecycle_reply(LifecycleResult result) const {
        return ReplyPayload{
            .result = static_cast<uint8_t>(result),
            .state = static_cast<uint8_t>(
                lifecycle_state_.load(std::memory_order_acquire)),
            .target = static_cast<uint8_t>(
                lifecycle_target_.load(std::memory_order_acquire)),
            .error_code = lifecycle_error_code_.load(std::memory_order_acquire)
        };
    }

    void flush_lifecycle_completion() {
        const auto request = pending_lifecycle_request_.load(std::memory_order_acquire);
        if (request == PendingLifecycleRequest::OnCompleted) {
            TimsMessage<LifecycleOnPayload> original{
                .header = pending_lifecycle_header_,
                .payload = {}
            };
            auto reply = make_lifecycle_reply<LifecycleOnReplyPayload>(
                lifecycle_state_.load(std::memory_order_acquire) == LifecycleState::Enabled
                    ? LifecycleResult::Success
                    : LifecycleResult::Failed);
            lifecycle_mailbox_->send_reply(original, reply);
            pending_lifecycle_request_.store(
                PendingLifecycleRequest::None, std::memory_order_release);
        } else if (request == PendingLifecycleRequest::OffCompleted) {
            TimsMessage<LifecycleOffPayload> original{
                .header = pending_lifecycle_header_,
                .payload = {}
            };
            auto reply = make_lifecycle_reply<LifecycleOffReplyPayload>(
                LifecycleResult::Success);
            lifecycle_mailbox_->send_reply(original, reply);
            pending_lifecycle_request_.store(
                PendingLifecycleRequest::None, std::memory_order_release);
        }
    }

    template<typename ReceivedMessage>
    void handle_lifecycle_request(const ReceivedMessage& received_message) {
        using Payload = typename ReceivedMessage::payload_type;

        if constexpr (std::is_same_v<Payload, GetLifecycleStatusPayload>) {
            LifecycleStatusReplyPayload reply{
                .state = static_cast<uint8_t>(
                    lifecycle_state_.load(std::memory_order_acquire)),
                .target = static_cast<uint8_t>(
                    lifecycle_target_.load(std::memory_order_acquire)),
                .error_code = lifecycle_error_code_.load(std::memory_order_acquire),
                .retry_count = 0,
                .state_since_ns = lifecycle_state_since_ns_.load(std::memory_order_acquire)
            };
            lifecycle_mailbox_->send_reply(received_message, reply);
        } else if constexpr (std::is_same_v<Payload, LifecycleOnPayload>) {
            if (lifecycle_state_.load(std::memory_order_acquire) == LifecycleState::Enabled) {
                auto reply = make_lifecycle_reply<LifecycleOnReplyPayload>(
                    LifecycleResult::AlreadyInState);
                lifecycle_mailbox_->send_reply(received_message, reply);
                return;
            }
            queue_lifecycle_request(
                PendingLifecycleRequest::OnRequested,
                LifecycleTarget::On,
                received_message);
        } else if constexpr (std::is_same_v<Payload, LifecycleOffPayload>) {
            if (lifecycle_state_.load(std::memory_order_acquire) == LifecycleState::Disabled) {
                auto reply = make_lifecycle_reply<LifecycleOffReplyPayload>(
                    LifecycleResult::AlreadyInState);
                lifecycle_mailbox_->send_reply(received_message, reply);
                return;
            }
            queue_lifecycle_request(
                PendingLifecycleRequest::OffRequested,
                LifecycleTarget::Off,
                received_message);
        }
    }

    template<typename ReceivedMessage>
    void queue_lifecycle_request(
        PendingLifecycleRequest requested,
        LifecycleTarget target,
        const ReceivedMessage& received_message) {
        auto expected = PendingLifecycleRequest::None;
        if (!pending_lifecycle_request_.compare_exchange_strong(
                expected, PendingLifecycleRequest::Writing,
                std::memory_order_acq_rel)) {
            using Payload = typename ReceivedMessage::payload_type;
            if constexpr (std::is_same_v<Payload, LifecycleOnPayload>) {
                auto reply = make_lifecycle_reply<LifecycleOnReplyPayload>(
                    LifecycleResult::Busy);
                lifecycle_mailbox_->send_reply(received_message, reply);
            } else {
                auto reply = make_lifecycle_reply<LifecycleOffReplyPayload>(
                    LifecycleResult::Busy);
                lifecycle_mailbox_->send_reply(received_message, reply);
            }
            return;
        }

        pending_lifecycle_header_ = received_message.header;
        lifecycle_target_.store(target, std::memory_order_release);
        pending_lifecycle_request_.store(requested, std::memory_order_release);
    }

    void lifecycle_loop() {
        while (!should_stop_.load(std::memory_order_acquire)) {
            flush_lifecycle_completion();
            lifecycle_mailbox_->receive_any_for(
                Milliseconds(10),
                [this](auto&& received_message) {
                    handle_lifecycle_request(received_message);
                });
        }
    }

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

    /**
     * @brief Start command threads with per-output names (compile-time index expansion)
     *
     * Uses template get_output<N>() to retrieve the CMD mailbox address for each
     * output at compile time, which is then embedded in the EVL thread name.
     * Name format: "<module>-cmd<N>/<cmd_addr_hex>"
     */
    template<size_t... OutputIndices>
    void start_command_threads(std::index_sequence<OutputIndices...>) {
        (start_command_thread<OutputIndices>(), ...);
    }

    template<size_t OutputIndex>
    void start_command_thread() {
        char tname[64];
        std::snprintf(tname, sizeof(tname), "%s-cmd%zu/%08X",
                      config_.name.c_str(),
                      OutputIndex,
                      this->template get_output<OutputIndex>().get_cmd_address());
        command_threads_[OutputIndex].start(ThreadConfig{.name = tname},
                                            [this]() { command_loop(OutputIndex); });
    }
};

} // namespace commrat
