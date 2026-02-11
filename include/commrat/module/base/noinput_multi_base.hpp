#pragma once

#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/module/traits/module_types.hpp"
#include "commrat/module/module_mixins.hpp"

namespace commrat {

/**
 * @brief Module base for no-input multi-output modules
 * 
 * Specialized implementation for modules that:
 * - Generate data without input (PeriodicInput or LoopInput)
 * - Produce multiple output types (Outputs<T, U, V...>)
 * 
 * Provides virtual function signature:
 *   void process(T& out1, U& out2, V& out3, ...)
 * 
 * Execution models:
 * - PeriodicInput: Runs at fixed period (config.period)
 * - LoopInput: Runs at maximum throughput (tight loop)
 * 
 * Both modes call process() with references to all output types.
 * Each output is wrapped in TimsMessage with timestamp = Time::now()
 */
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class NoInputMultiOutputBase
    // Only inherit what we need for this mode:
    : public MultiOutputProcessorBase<typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type, void>
    , public LifecycleManager<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public LoopExecutor<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public MailboxInfrastructureBuilder<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry>
    , public InputMetadataManager<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public InputMetadataAccessors<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public CommandDispatcher<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>
    , public MultiOutputManager<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>
    , public WorkLoopHandler<NoInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
{
    // Friend declarations for CRTP mixins
    friend class LifecycleManager<NoInputMultiOutputBase>;
    friend class LoopExecutor<NoInputMultiOutputBase>;
    friend class MailboxInfrastructureBuilder<NoInputMultiOutputBase, UserRegistry>;
    friend class InputMetadataManager<NoInputMultiOutputBase>;
    friend class InputMetadataAccessors<NoInputMultiOutputBase>;
    friend class CommandDispatcher<NoInputMultiOutputBase, CommandTypes...>;
    friend class MultiOutputManager<NoInputMultiOutputBase, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>;
    friend class WorkLoopHandler<NoInputMultiOutputBase>;
    
private:
    // ========================================================================
    // Type Definitions
    // ========================================================================
    
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
    
    // Mailbox types
    using CmdMailbox = typename ModuleTypes::CmdMailbox;
    using WorkMailbox = typename ModuleTypes::WorkMailbox;
    using PublishMailbox = typename ModuleTypes::PublishMailbox;
    
    // Counts and flags
    static constexpr size_t num_output_types = ModuleTypes::num_output_types;
    static constexpr size_t InputCount = ModuleTypes::InputCount;
    
    // Compile-time validation
    static_assert(ModuleTypes::has_periodic_input || ModuleTypes::has_loop_input, 
                  "NoInputMultiOutputBase requires PeriodicInput or LoopInput");
    static_assert(ModuleTypes::has_multi_output, "NoInputMultiOutputBase requires Outputs<Ts...>");
    static_assert(!ModuleTypes::has_multi_input, "NoInputMultiOutputBase does not support multi-input");
    static_assert(!ModuleTypes::has_continuous_input, "NoInputMultiOutputBase does not support continuous input");
    
public:
    // Public type aliases (user-visible)
    using Registry = UserRegistry;
    using OutputData = void;  // Multi-output has no single OutputData
    using InputData = void;   // No input
    
    // Mode flags (for introspection)
    static constexpr bool has_periodic_input = ModuleTypes::has_periodic_input;
    static constexpr bool has_continuous_input = false;
    static constexpr bool has_loop_input = ModuleTypes::has_loop_input;
    static constexpr bool has_multi_input = false;
    static constexpr bool has_multi_output = true;
    
protected:
    ModuleConfig config_;
    
    // Mailbox infrastructure (one set per output type)
    MailboxSetTuple mailbox_infrastructure_;
    
    // Compatibility with LifecycleManager (expects data_mailbox_)
    std::unique_ptr<PublishMailbox> data_mailbox_ = nullptr;
    
    // Helper accessors
    CmdMailbox& cmd_mailbox() {
        return *std::get<0>(mailbox_infrastructure_).cmd;
    }
    
    WorkMailbox& work_mailbox() {
        return *std::get<0>(mailbox_infrastructure_).work;
    }
    
    PublishMailbox& publish_mailbox() {
        return *std::get<0>(mailbox_infrastructure_).publish;
    }
    
    template<std::size_t Index>
    auto& get_mailbox_set() {
        return std::get<Index>(mailbox_infrastructure_);
    }
    
    template<std::size_t Index>
    auto& get_publish_mailbox() {
        return *std::get<Index>(mailbox_infrastructure_).publish;
    }
    
    template<std::size_t Index>
    auto& get_work_mailbox() {
        return *std::get<Index>(mailbox_infrastructure_).work;
    }
    
    template<std::size_t Index>
    auto& get_cmd_mailbox() {
        return *std::get<Index>(mailbox_infrastructure_).cmd;
    }
    
    // Subscription protocol
    using SubscriptionProtocolType = commrat::SubscriptionProtocol<
        UserRegistry,
        false,  // has_continuous_input
        false,  // has_multi_input
        void,   // InputData
        InputTypesTuple,
        InputCount,
        void,   // OutputData (multi-output)
        OutputTypesTuple
    >;
    SubscriptionProtocolType subscription_protocol_;
    
    std::atomic<bool> running_;
    
    // Threads
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    
    // Input metadata (unused for no-input modes)
    static constexpr std::size_t num_inputs = InputCount_v<InputSpec>;
    std::array<InputMetadataStorage, (num_inputs > 0 ? num_inputs : 1)> input_metadata_;
    
    // Helper: Initialize mailbox infrastructure
    template<std::size_t... Is>
    void initialize_mailbox_infrastructure_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        ((std::get<Is>(mailbox_infrastructure_).initialize(config)), ...);
    }

public:
    explicit NoInputMultiOutputBase(const ModuleConfig& config)
        : config_(config)
        , mailbox_infrastructure_{}
        , running_(false)
    {
        // Initialize mailbox infrastructure (one set per output type)
        initialize_mailbox_infrastructure_impl(config, std::make_index_sequence<num_output_types>{});
        
        // Initialize per-output subscriber lists
        this->initialize_output_subscribers();
        
        // Initialize subscription protocol
        subscription_protocol_.set_config(&config_);
        subscription_protocol_.set_work_mailbox(&work_mailbox());
        subscription_protocol_.set_module_name(config.name);
    }
    
    virtual ~NoInputMultiOutputBase() {
        this->stop();
    }
    
    NoInputMultiOutputBase(const NoInputMultiOutputBase&) = delete;
    NoInputMultiOutputBase& operator=(const NoInputMultiOutputBase&) = delete;
    NoInputMultiOutputBase(NoInputMultiOutputBase&&) = delete;
    NoInputMultiOutputBase& operator=(NoInputMultiOutputBase&&) = delete;
    
    // ========================================================================
    // Lifecycle Hooks (Override in derived class)
    // ========================================================================
    
    virtual void on_init() {}
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_cleanup() {}
    
    // ========================================================================
    // Public Mailbox Accessors
    // ========================================================================
    
    template<std::size_t Index>
    auto& get_publish_mailbox_public() {
        return *std::get<Index>(mailbox_infrastructure_).publish;
    }
    
    // ========================================================================
    // Subscription Protocol
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

public:
    // Subscription protocol handlers - PUBLIC for callbacks
    void handle_subscribe_request(const SubscribeRequestPayload& req) {
        subscription_protocol_.handle_subscribe_request(req, *this);
    }
    
    void handle_subscribe_reply(const SubscribeReplyPayload& reply) {
        subscription_protocol_.handle_subscribe_reply(reply);
    }
    
    void handle_unsubscribe_request(const UnsubscribeRequestPayload& req) {
        subscription_protocol_.handle_unsubscribe_request(req, *this);
    }
    
    void add_subscriber(uint32_t subscriber_base_addr) {
        this->MultiOutputManager<NoInputMultiOutputBase, UserRegistry, OutputTypesTuple>::add_subscriber_to_output(subscriber_base_addr);
    }
    
    void remove_subscriber(uint32_t subscriber_base_addr) {
        this->MultiOutputManager<NoInputMultiOutputBase, UserRegistry, OutputTypesTuple>::remove_subscriber(subscriber_base_addr);
    }
    
protected:
    // ========================================================================
    // Multi-Output Publishing
    // ========================================================================
    
    template<typename... Ts>
    void publish_multi_outputs_with_timestamp(std::tuple<Ts...>& outputs, uint64_t timestamp_ns) {
        publish_multi_outputs_impl(outputs, timestamp_ns, std::index_sequence_for<Ts...>{});
    }
    
private:
    template<typename TupleType, std::size_t... Is>
    void publish_multi_outputs_impl(TupleType& outputs, uint64_t timestamp_ns, std::index_sequence<Is...>) {
        // Publish each output to its respective subscribers
        (publish_single_output<Is>(std::get<Is>(outputs), timestamp_ns), ...);
    }
    
    template<std::size_t Index, typename T>
    void publish_single_output(T& data, uint64_t timestamp_ns) {
        // Create TimsMessage with provided timestamp
        TimsMessage<T> tims_msg;
        tims_msg.header.timestamp = timestamp_ns;
        tims_msg.header.seq_number = 0;  // TODO: per-output sequence numbers
        tims_msg.header.msg_type = UserRegistry::template get_message_id<T>();
        tims_msg.payload = data;
        
        // Get subscribers for this output type
        auto& subscribers = this->template get_output_subscribers<Index>();
        
        // Serialize once
        auto serialized = sertial::Message<TimsMessage<T>>::serialize(tims_msg);
        if (!serialized) {
            return;  // Serialization failed
        }
        
        // Send to all subscribers of this output type
        auto& publish_mbx = get_publish_mailbox<Index>();
        for (uint32_t subscriber_addr : subscribers) {
            publish_mbx.send_bytes(serialized.view(), subscriber_addr + 32);
        }
    }
};

} // namespace commrat
