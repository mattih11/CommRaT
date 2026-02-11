#pragma once

#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"  // Phase 7.3: Type-optimized mailboxes
#include "commrat/mailbox/historical_mailbox.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/platform/threading.hpp"
#include "commrat/platform/timestamp.hpp"
#include "commrat/module/io_spec.hpp"
#include "commrat/module/module_config.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include "commrat/module/traits/multi_input_resolver.hpp"
#include "commrat/module/traits/module_types.hpp"  // Phase 1: Extracted type computations
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/helpers/tims_helpers.hpp"
#include "commrat/module/metadata/input_metadata.hpp"
#include "commrat/module/metadata/input_metadata_accessors.hpp"  // Phase 5: Metadata accessor mixin
#include "commrat/module/subscription.hpp"
#include "commrat/module/publishing.hpp"
#include "commrat/module/loops/loop_executor.hpp"  // Phase 5: Loop implementations (must be before Module)
#include "commrat/module/mailbox_set.hpp"  // Phase 7.4: Per-output-type mailbox sets
#include "commrat/module/multi_output_manager.hpp"  // Phase 2: Multi-output management mixin
#include "commrat/module/command_dispatcher.hpp"  // Phase 3: Command dispatch mixin
#include "commrat/module/multi_input_infrastructure.hpp"  // Phase 4: Multi-input mailbox infrastructure
#include "commrat/module/multi_input_processor.hpp"  // Phase 5: Multi-input processing helpers
#include "commrat/module/lifecycle_manager.hpp"  // Phase 6: Lifecycle management (start/stop)
#include "commrat/module/work_loop_handler.hpp"  // Phase 7: Work loop (subscription protocol)
#include <sertial/sertial.hpp>
#include <atomic>
#include <vector>
#include <functional>
#include <optional>

namespace commrat {

// Empty base classes for conditional inheritance
struct EmptyBase {};
struct EmptyBase2 {};  // Need separate empty bases to avoid duplicate base class error
struct EmptyBase3 {};
struct EmptyBase4 {};

// ============================================================================
// Phase 6.10: Automatic Timestamp Management
// ============================================================================

/**
 * @brief Phase 6.10: Timestamp Management Architecture
 * 
 * Timestamps are stored ONLY in TimsHeader.timestamp, never in payload.
 * Module automatically wraps payloads in TimsMessage and sets header.timestamp:
 * - PeriodicInput/LoopInput: timestamp = Time::now() (data generation time)
 * - Multi-input: timestamp = primary_input.header.timestamp (sync point)
 * - ContinuousInput: timestamp = input.header.timestamp (propagation)
 * 
 * Users never deal with timestamps - messages are clean data structures.
 */

// ============================================================================
// Registry-Based Module (New Clean Interface)
// ============================================================================

/**
 * @brief Modern C++20 module base using MessageRegistry
 * 
 * Users define their complete application message registry once, then Module
 * automatically includes subscription protocol messages and uses the registry.
 * 
 * @tparam UserRegistry The application's complete MessageRegistry
 * @tparam OutputSpec_ Output specification: Output<T>, Outputs<Ts...>, or raw type T (normalized to Output<T>)
 * @tparam InputSpec_ Input specification: Input<T>, PeriodicInput, LoopInput, ContinuousInput<T> (legacy)
 * @tparam CommandTypes Optional variadic command types this module handles
 * 
 * Example (new style with I/O specs):
 * @code
 * class SensorModule : public Module<Registry, Output<TempData>, PeriodicInput> {
 * protected:
 *     TempData process() override {
 *         return TempData{.temperature_celsius = read_sensor()};
 *     }
 * };
 * @endcode
 * 
 * Example (backward compatible - raw type):
 * @code
 * class SensorModule : public Module<Registry, TempData, PeriodicInput> {
 * protected:
 *     TempData process() override {
 *         return TempData{.temperature_celsius = read_sensor()};
 *     }
 * };
 * @endcode
 */
// Helper to resolve MultiInputProcessorBase template arguments
// (workaround for complex nested template parsing issues)
// Note: Now imported from module/traits/multi_input_resolver.hpp

template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class Module 
    : public ContinuousProcessorBase<
        typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type,
        typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type
      >
    , public MultiOutputProcessorBase<
        typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type,
        typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type
      >
    , public SingleOutputProcessorBase<
        typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type
      >
    , public ResolveMultiInputBase<InputSpec_, OutputSpec_>::type
    , public commrat::SubscriberManager // Subscriber management for subscription protocol
    , public std::conditional_t<
        (module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::num_output_types > 1),
        MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>,
        EmptyBase  // Single-output modules don't need MultiOutputManager
      >
    , public LoopExecutor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>  // Loop implementations for periodic and free loops
    , public InputMetadataAccessors<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>  // Input metadata accessors
    , public CommandDispatcher<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>  // Phase 3: Command dispatch
    , public std::conditional_t<
        module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::has_multi_input,
        MultiInputInfrastructure<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>,
        EmptyBase2  // Single-input modules don't need multi-input infrastructure
      >  // Phase 4: Multi-input infrastructure
    , public std::conditional_t<
        module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::has_multi_input,
        MultiInputProcessor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputData, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>,
        EmptyBase3  // Single-input modules don't need multi-input processor
      >  // Phase 5: Multi-input processing
    , public LifecycleManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>  // Phase 6: Lifecycle management
    , public WorkLoopHandler<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>  // Phase 7: Work loop handler
{
    // Friend declarations for CRTP mixins
    friend class LoopExecutor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>;
    friend class InputMetadataAccessors<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class CommandDispatcher<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>;
    friend class MultiInputInfrastructure<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>;
    friend class MultiInputProcessor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputData, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>;
    friend class LifecycleManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class WorkLoopHandler<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    
private:
    // ========================================================================
    // Type Definitions (Phase 1: Extracted to module_types.hpp)
    // ========================================================================
    
    // Import all type computations from module_traits::ModuleTypes
    using ModuleTypes = module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>;
    
    // Normalized specs
    using OutputSpec = typename ModuleTypes::OutputSpec;
    using InputSpec = typename ModuleTypes::InputSpec;
    
    // Type tuples
    using OutputTypesTuple = typename ModuleTypes::OutputTypesTuple;
    using InputTypesTuple = typename ModuleTypes::InputTypesTuple;
    using MailboxSetTuple = typename ModuleTypes::MailboxSetTuple;
    using CommandTuple = typename ModuleTypes::CommandTuple;
    using CombinedCmdTypes = typename ModuleTypes::CombinedCmdTypes;
    using DataTypesTuple = typename ModuleTypes::DataTypesTuple;
    
    // Mailbox types
    using CmdMailbox = typename ModuleTypes::CmdMailbox;
    using WorkMailbox = typename ModuleTypes::WorkMailbox;
    using PublishMailbox = typename ModuleTypes::PublishMailbox;
    using DataMailbox = typename ModuleTypes::DataMailbox;
    
    // Counts and flags
    static constexpr size_t num_output_types = ModuleTypes::num_output_types;
    static constexpr size_t InputCount = ModuleTypes::InputCount;
    static constexpr bool has_multi_input = ModuleTypes::has_multi_input;
    static constexpr bool use_mailbox_sets = ModuleTypes::use_mailbox_sets;
    static constexpr bool has_primary_input_spec = ModuleTypes::has_primary_input_spec;
    using PrimaryPayloadType = typename ModuleTypes::PrimaryPayloadType;
    
    // Compile-time validation: If PrimaryInput<T> specified, T must be in Inputs<...>
    template<typename Dummy = void>
    struct ValidatePrimaryInputImpl {
        static constexpr bool check() {
            if constexpr (has_primary_input_spec && has_multi_input) {
                // This will trigger PrimaryInputIndex's static_assert if type not found
                return PrimaryInputIndex_v<PrimaryPayloadType, InputTypesTuple> >= 0;
            }
            return true;
        }
        static constexpr bool value = check();
    };
    static_assert(ValidatePrimaryInputImpl<>::value, 
                  "PrimaryInput validation failed - see error above for details");
    
public:
    // Public type aliases (user-visible)
    using OutputData = typename ModuleTypes::OutputData;
    using InputData = typename ModuleTypes::InputData;
    
    // Input mode flags
    static constexpr bool has_continuous_input = ModuleTypes::has_continuous_input;
    static constexpr bool has_periodic_input = ModuleTypes::has_periodic_input;
    static constexpr bool has_loop_input = ModuleTypes::has_loop_input;
    static constexpr bool has_multi_output = ModuleTypes::has_multi_output;
    
protected:
    ModuleConfig config_;
    
    // Phase 7.4: Per-output mailbox infrastructure
    // For single output: Direct mailboxes (backward compatible)
    // For multi-output: Tuple of MailboxSets (each output at its own base address)
    std::conditional_t<
        use_mailbox_sets,
        MailboxSetTuple,  // Multi-output: tuple of MailboxSets
        std::tuple<CmdMailbox, WorkMailbox, PublishMailbox>  // Single-output: wrapped in tuple for uniform access
    > mailbox_infrastructure_;
    
    // Helper accessors for single-output case (backward compatible)
    CmdMailbox& cmd_mailbox() {
        if constexpr (!use_mailbox_sets) {
            return std::get<0>(mailbox_infrastructure_);
        } else {
            // Multi-output: return first MailboxSet's cmd mailbox
            return *std::get<0>(mailbox_infrastructure_).cmd;
        }
    }
    
    WorkMailbox& work_mailbox() {
        if constexpr (!use_mailbox_sets) {
            return std::get<1>(mailbox_infrastructure_);
        } else {
            // Multi-output: return first MailboxSet's work mailbox
            return *std::get<0>(mailbox_infrastructure_).work;
        }
    }
    
    PublishMailbox& publish_mailbox() {
        static_assert(!use_mailbox_sets, "publish_mailbox() accessor only available for single-output modules. Use get_publish_mailbox_public<Index>() for multi-output");
        return std::get<2>(mailbox_infrastructure_);
    }
    
    // Multi-output accessor - get specific MailboxSet by index
    template<std::size_t Index>
    auto& get_mailbox_set() {
        static_assert(use_mailbox_sets, "get_mailbox_set() only available for multi-output modules");
        return std::get<Index>(mailbox_infrastructure_);
    }
    
    // Get publish mailbox by index for multi-output modules
    template<std::size_t Index>
    auto& get_publish_mailbox() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).publish;
        } else {
            static_assert(Index == 0, "Single-output modules only have one publish mailbox (index 0)");
            return std::get<2>(mailbox_infrastructure_);
        }
    }
    
    // Get work mailbox by index for multi-output modules
    template<std::size_t Index>
    auto& get_work_mailbox() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).work;
        } else {
            static_assert(Index == 0, "Single-output modules only have one work mailbox (index 0)");
            return std::get<1>(mailbox_infrastructure_);
        }
    }
    
    // Get cmd mailbox by index for multi-output modules
    template<std::size_t Index>
    auto& get_cmd_mailbox() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).cmd;
        } else {
            static_assert(Index == 0, "Single-output modules only have one cmd mailbox (index 0)");
            return std::get<0>(mailbox_infrastructure_);
        }
    }
    
    // Phase 6.6: Multi-input support
    // Single-input mode (backward compatible)
    std::optional<DataMailbox> data_mailbox_;  // base + 48: Receives input data (only for single ContinuousInput)
    
    // Phase 4: Multi-input mailbox infrastructure moved to MultiInputInfrastructure mixin
    // - input_mailboxes_: Tuple of HistoricalMailbox instances
    // - secondary_input_threads_: Background receive threads
    
    // Subscription protocol (Phase 5: extracted to subscription.hpp)
    using SubscriptionProtocolType = commrat::SubscriptionProtocol<
        UserRegistry,
        has_continuous_input,
        has_multi_input,
        InputData,
        InputTypesTuple,
        InputCount,
        OutputData,
        OutputTypesTuple
    >;
    SubscriptionProtocolType subscription_protocol_;
    
    // Publishing logic (Phase 5: extracted to publishing.hpp)
    // Phase 7.4: Add Module type for multi-output mailbox access
    using PublisherType = commrat::Publisher<
        UserRegistry, 
        OutputData, 
        commrat::SubscriberManager, 
        PublishMailbox,
        Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>  // Module type for get_publish_mailbox<Index>()
    >;
    PublisherType publisher_;
    
    std::atomic<bool> running_;
    
    // Module threads
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    std::optional<std::thread> work_thread_;              // Single-output: one WORK mailbox
    // Phase 2: output_work_threads_ moved to MultiOutputManager mixin
    // Phase 4: secondary_input_threads_ moved to MultiInputInfrastructure mixin
    
    // Subscriber management - inherited from SubscriberManager base class
    // (subscribers_, subscribers_mutex_ now in SubscriberManager)
    
    // Phase 2: Multi-output subscriber management moved to MultiOutputManager mixin
    // - output_subscribers_: Per-output subscriber lists
    // - output_subscribers_mutex_: Protects output_subscribers_
    
    // ========================================================================
    // Phase 6.10: Input Metadata Storage
    // ========================================================================
    
    /**
     * @brief Storage for input message metadata
     * 
     * Stores metadata about received input messages, populated before process() calls.
     * Available to derived classes via get_input_metadata<Index>() accessors.
     * 
     * Note: InputMetadataStorage defined in commrat/module/metadata/input_metadata.hpp
     */
    
    // Calculate number of inputs at compile time
    static constexpr std::size_t num_inputs = InputCount_v<InputSpec>;
    
    // Fixed-size array for input metadata (zero-size when no inputs)
    std::array<InputMetadataStorage, (num_inputs > 0 ? num_inputs : 1)> input_metadata_;
    
    /**
     * @brief Update metadata for a specific input index
     * 
     * Helper method to populate metadata from received TimsMessage.
     * Called by loop functions before invoking process() methods.
     */
    template<typename T>
    void update_input_metadata(std::size_t index, const TimsMessage<T>& received, bool is_new) {
        if (index >= num_inputs) {
            std::cerr << "[Module] ERROR: Invalid metadata index " << index << "\n";
            return;
        }
        
        input_metadata_[index].timestamp = received.header.timestamp;
        input_metadata_[index].sequence_number = received.header.seq_number;
        input_metadata_[index].message_id = received.header.msg_type;
        input_metadata_[index].is_new_data = is_new;
        input_metadata_[index].is_valid = true;
    }
    
    /**
     * @brief Mark input metadata as invalid (getData failed)
     */
    void mark_input_invalid(std::size_t index) {
        if (index >= num_inputs) {
            return;
        }
        input_metadata_[index].is_valid = false;
        input_metadata_[index].is_new_data = false;
    }
    
    /**
     * @brief Helper to create mailbox infrastructure (Phase 7.4)
     * 
     * Single output: Returns tuple<CmdMailbox, WorkMailbox, PublishMailbox>
     * Multi-output: Returns tuple<MailboxSet<T1>, MailboxSet<T2>, ...>
     */
    static auto create_mailbox_infrastructure(const ModuleConfig& config) {
        if constexpr (!use_mailbox_sets) {
            // Single output: Create traditional mailboxes
            CmdMailbox cmd(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::CMD),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_cmd"
            });
            
            WorkMailbox work(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::WORK),
                .message_slots = config.message_slots,
                .max_message_size = SystemRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_work"
            });
            
            PublishMailbox publish(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::PUBLISH),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_publish"
            });
            
            return std::make_tuple(std::move(cmd), std::move(work), std::move(publish));
        } else {
            // Multi-output: Create tuple of MailboxSets
            return create_mailbox_sets_impl(config, std::make_index_sequence<num_output_types>{});
        }
    }
    
    /**
     * @brief Helper to create MailboxSet tuple for multi-output modules
     */
    template<std::size_t... Is>
    static auto create_mailbox_sets_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        // Use fold expression to construct tuple with initialized MailboxSets
        return MailboxSetTuple{create_mailbox_set<Is>(config)...};
    }
    
    /**
     * @brief Create a single MailboxSet for output type at index I
     */
    template<std::size_t I>
    static auto create_mailbox_set(const ModuleConfig& config) {
        using OutputType = std::tuple_element_t<I, OutputTypesTuple>;
        MailboxSet<UserRegistry, OutputType, CommandTypes...> set;
        set.initialize(config);
        return set;
    }
    
public:
    explicit Module(const ModuleConfig& config)
        : config_(config)
        , mailbox_infrastructure_(create_mailbox_infrastructure(config))
        , data_mailbox_(has_continuous_input && !has_multi_input ? 
            std::make_optional<DataMailbox>(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::DATA),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_data"
            }) : 
            std::nullopt)
        , running_(false)
    {
        // Initialize SubscriberManager
        this->set_max_subscribers(config.max_subscribers);
        
        // Phase 2: Initialize per-output subscriber lists for multi-output modules
        if constexpr (use_mailbox_sets) {
            this->initialize_output_subscribers();
        }
        
        // Initialize subscription protocol
        subscription_protocol_.set_config(&config_);
        subscription_protocol_.set_work_mailbox(&work_mailbox());
        subscription_protocol_.set_module_name(config.name);
        
        // Initialize publisher
        publisher_.set_subscriber_manager(this);  // Module inherits from SubscriberManager
        publisher_.set_module_ptr(this);  // For multi-output mailbox access
        
        // Only set publish mailbox for single-output modules
        if constexpr (!use_mailbox_sets) {
            publisher_.set_publish_mailbox(&publish_mailbox());
        }
        // Multi-output modules access mailboxes via get_publish_mailbox<Index>()
        
        publisher_.set_module_name(config.name);
        
        // Phase 6.6: Initialize multi-input mailboxes
        if constexpr (has_multi_input) {
            this->initialize_multi_input_mailboxes();
        }
    }
    
    virtual ~Module() {
        this->stop();
    }
    
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;
    
    // ========================================================================
    // Lifecycle Hooks (Override These)
    // ========================================================================
    
    virtual void on_init() {}
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_cleanup() {}
    
    // ========================================================================
    // Processing Hooks (Override Based on InputMode)
    // ========================================================================

protected:
    // For ContinuousInput: process input and return output
    // Calls process_continuous which derived classes should hide/override
    template<typename T = InputData>
    OutputData process_continuous_dispatch(const T& input) {
        // This->process_continuous will resolve to derived class version via name hiding
        return this->process_continuous(input);
    }
    
    // Derived classes define this (non-template) for continuous input:
    //   OutputData process_continuous(const InputData& input) { ... }


public:
    // ========================================================================
    // Phase 7.4: Public Mailbox Accessors for Multi-Output
    // ========================================================================
    
    /**
     * @brief Get publish mailbox by index (public for Publisher access)
     */
    template<std::size_t Index>
    auto& get_publish_mailbox_public() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).publish;
        } else {
            static_assert(Index == 0, "Single-output modules only have one publish mailbox (index 0)");
            return std::get<2>(mailbox_infrastructure_);
        }
    }
    
    // ========================================================================
    // Lifecycle Management (Phase 6: Extracted to lifecycle_manager.hpp)
    // ========================================================================
    // start(), stop(), is_running() moved to LifecycleManager mixin

    // ========================================================================
    // Subscription Protocol (delegated to SubscriptionProtocol member)
    // ========================================================================
protected:
    void subscribe_to_all_sources() {
        subscription_protocol_.subscribe_to_all_sources();
    }
    
    void subscribe_to_source(uint8_t source_system_id, uint8_t source_instance_id) {
        subscription_protocol_.subscribe_to_source(source_system_id, source_instance_id);
    }
    
    void unsubscribe_from_source(uint8_t source_system_id, uint8_t source_instance_id) {
        subscription_protocol_.unsubscribe_from_source(source_system_id, source_instance_id);
    }
    
    void unsubscribe_from_multi_input_source(const InputSource& source) {
        subscription_protocol_.unsubscribe_from_multi_input_source(source);
    }
    
    void handle_subscribe_request(const SubscribeRequestPayload& req) {
        subscription_protocol_.handle_subscribe_request(req, static_cast<commrat::SubscriberManager&>(*this));
    }
    
    void handle_subscribe_reply(const SubscribeReplyPayload& reply) {
        subscription_protocol_.handle_subscribe_reply(reply);
    }
    
    void handle_unsubscribe_request(const UnsubscribeRequestPayload& req) {
        subscription_protocol_.handle_unsubscribe_request(req, static_cast<commrat::SubscriberManager&>(*this));
    }
    
    /**
     * @brief Phase 2: Add subscriber to correct output-specific list
     * 
     * Delegates to MultiOutputManager mixin for multi-output modules.
     */
    void add_subscriber_to_output(uint32_t subscriber_base_addr) {
        if constexpr (use_mailbox_sets) {
            // Multi-output: delegate to inherited MultiOutputManager::add_subscriber_to_output
            this->MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, OutputTypesTuple>::add_subscriber_to_output(subscriber_base_addr);
        } else {
            // Single-output: use inherited SubscriberManager::add_subscriber
            this->add_subscriber(subscriber_base_addr);
        }
    }
    
protected:

    // ========================================================================
    // Publishing (Phase 5: extracted to publishing.hpp)
    // ========================================================================
    // Delegation wrappers for publishing operations
    
    template<typename T = OutputData>
        requires (!std::is_void_v<T>)
    void publish_to_subscribers(T& data) {
        publisher_.publish_to_subscribers(data);
    }
    
    template<typename T>
    void publish_tims_message(TimsMessage<T>& tims_msg) {
        publisher_.publish_tims_message(tims_msg);
    }
    
    template<typename... Ts>
    void publish_multi_outputs(std::tuple<Ts...>& outputs) {
        publisher_.publish_multi_outputs(outputs);
    }
    
    template<typename... Ts>
    void publish_multi_outputs_with_timestamp(std::tuple<Ts...>& outputs, uint64_t timestamp_ns) {
        publisher_.publish_multi_outputs_with_timestamp(outputs, timestamp_ns);
    }
    
    template<typename T>
    static TimsMessage<T> create_tims_message(T&& payload, uint64_t timestamp_ns) {
        return PublisherType::create_tims_message(std::forward<T>(payload), timestamp_ns);
    }

    // ========================================================================
    // Main Loops
    // ========================================================================
    // See: commrat/module/loops/loop_executor.hpp
    // - periodic_loop(): Time-driven generation (PeriodicInput)
    // - free_loop(): Maximum throughput (LoopInput)
    // - continuous_loop(): Event-driven single input (Input<T>)
    // - multi_input_loop(): Synchronized multi-input (Inputs<T,U,V>)
    // ========================================================================
    
private:
    // Phase 4: secondary_input_receive_loop() moved to MultiInputInfrastructure mixin
    // Phase 3: command_loop() moved to CommandDispatcher mixin
    // Phase 7: work_loop() moved to WorkLoopHandler mixin
    
    // Phase 2: output_work_loop moved to MultiOutputManager mixin
    // Phase 3: handle_user_command() and on_command() moved to CommandDispatcher mixin
    
    // ========================================================================
    // Helper Functions
    // ========================================================================
    
private:
    // Helper: Get primary input index at compile time
    static constexpr size_t get_primary_input_index() {
        // Phase 6.7: Extract primary input from CommandTypes (PrimaryInput<T> passed as command)
        if constexpr (has_primary_input_spec) {
            // PrimaryInput<T> found in CommandTypes, extract T and find index
            return PrimaryInputIndex_v<PrimaryPayloadType, InputTypesTuple>;
        } else {
            // No explicit PrimaryInput - use first input as primary
            return 0;
        }
    }
    
    // Phase 4: Multi-input mailbox creation/initialization moved to MultiInputInfrastructure mixin
    // - create_historical_mailbox_for_input<Index>()
    // - create_input_mailboxes_impl()
    // - initialize_multi_input_mailboxes()
    // - start_input_mailboxes_impl()
    // - start_input_mailboxes()
    // - start_secondary_input_threads()
    // - start_secondary_threads_impl()
    
    // Phase 5: Multi-input processing moved to MultiInputProcessor mixin
    // - receive_primary_input<PrimaryIdx>()
    // - gather_all_inputs<PrimaryIdx>()
    // - sync_secondary_inputs<PrimaryIdx>()
    // - sync_secondary_inputs_impl<PrimaryIdx>()
    // - sync_input_at_index<Index>()
    // - call_multi_input_process()
    // - call_multi_input_process_impl()
    // - call_multi_input_multi_output_process()
    // - call_multi_input_multi_output_process_impl()
};

} // namespace commrat
