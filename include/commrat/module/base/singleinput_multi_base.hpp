#pragma once

#include "commrat/module/base/noinput_multi_base.hpp"

namespace commrat {

/**
 * @brief Module base for single-input multi-output modules
 * 
 * Input<T> → Outputs<U, V, W...>
 * 
 * Inherits from NoInputMultiOutputBase and adds data mailbox for Input<T>.
 * The LoopExecutor's continuous_loop() handles input processing.
 */
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class SingleInputMultiOutputBase
    : public MultiOutputProcessorBase<typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type, typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type>
    , public LifecycleManager<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public LoopExecutor<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public MailboxInfrastructureBuilder<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry>
    , public InputMetadataManager<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public InputMetadataAccessors<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public CommandDispatcher<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>
    , public MultiOutputManager<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>
    , public WorkLoopHandler<SingleInputMultiOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
{
    friend class LifecycleManager<SingleInputMultiOutputBase>;
    friend class LoopExecutor<SingleInputMultiOutputBase>;
    friend class MailboxInfrastructureBuilder<SingleInputMultiOutputBase, UserRegistry>;
    friend class InputMetadataManager<SingleInputMultiOutputBase>;
    friend class InputMetadataAccessors<SingleInputMultiOutputBase>;
    friend class CommandDispatcher<SingleInputMultiOutputBase, CommandTypes...>;
    friend class MultiOutputManager<SingleInputMultiOutputBase, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>;
    friend class WorkLoopHandler<SingleInputMultiOutputBase>;
    
private:
    using ModuleTypes = module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>;
    using OutputSpec = typename ModuleTypes::OutputSpec;
    using InputSpec = typename ModuleTypes::InputSpec;
    using OutputTypesTuple = typename ModuleTypes::OutputTypesTuple;
    using InputTypesTuple = typename ModuleTypes::InputTypesTuple;
    using MailboxSetTuple = typename ModuleTypes::MailboxSetTuple;
    using DataMailbox = typename ModuleTypes::DataMailbox;
    
    static constexpr size_t num_output_types = ModuleTypes::num_output_types;
    
    static_assert(ModuleTypes::has_continuous_input, "SingleInputMultiOutputBase requires Input<T>");
    static_assert(ModuleTypes::has_multi_output, "SingleInputMultiOutputBase requires Outputs<Ts...>");
    
public:
    using Registry = UserRegistry;
    using OutputData = void;  // Multi-output
    using InputData = typename ExtractInputPayload<InputSpec>::type;
    
    static constexpr bool has_periodic_input = false;
    static constexpr bool has_continuous_input = true;
    static constexpr bool has_loop_input = false;
    static constexpr bool has_multi_input = false;
    static constexpr bool has_multi_output = true;
    
protected:
    ModuleConfig config_;
    MailboxSetTuple mailbox_infrastructure_;
    std::unique_ptr<DataMailbox> data_mailbox_;
    
    using SubscriptionProtocolType = commrat::SubscriptionProtocol<
        UserRegistry, true, false, InputData, InputTypesTuple, 1, void, OutputTypesTuple
    >;
    SubscriptionProtocolType subscription_protocol_;
    
    std::atomic<bool> running_;
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    std::array<InputMetadataStorage, 1> input_metadata_;
    
    CmdMailbox& cmd_mailbox() { return *std::get<0>(mailbox_infrastructure_).cmd; }
    WorkMailbox& work_mailbox() { return *std::get<0>(mailbox_infrastructure_).work; }
    
    template<std::size_t Index>
    auto& get_mailbox_set() { return std::get<Index>(mailbox_infrastructure_); }
    
    template<std::size_t Index>
    auto& get_publish_mailbox() { return *std::get<Index>(mailbox_infrastructure_).publish; }
    
    template<std::size_t... Is>
    void initialize_mailbox_infrastructure_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        ((std::get<Is>(mailbox_infrastructure_).initialize(config)), ...);
    }
    
public:
    explicit SingleInputMultiOutputBase(const ModuleConfig& config)
        : config_(config), mailbox_infrastructure_{}, running_(false)
    {
        initialize_mailbox_infrastructure_impl(config, std::make_index_sequence<num_output_types>{});
        
        uint32_t base_address = calculate_base_address(config.system_id, config.instance_id);
        data_mailbox_ = std::make_unique<DataMailbox>(
            UserRegistry::template get_message_id<InputData>(),
            base_address + 32,
            config.message_slots
        );
        
        this->initialize_output_subscribers();
        subscription_protocol_.set_config(&config_);
        subscription_protocol_.set_work_mailbox(&work_mailbox());
        subscription_protocol_.set_module_name(config.name);
    }
    
    virtual ~SingleInputMultiOutputBase() { this->stop(); }
    
    SingleInputMultiOutputBase(const SingleInputMultiOutputBase&) = delete;
    SingleInputMultiOutputBase& operator=(const SingleInputMultiOutputBase&) = delete;
    
    virtual void on_init() {}
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_cleanup() {}
    
protected:
    void subscribe_to_source(uint8_t source_system_id, uint8_t source_instance_id) {
        subscription_protocol_.subscribe_to_source(source_system_id, source_instance_id);
    }
    
public:
    void handle_subscribe_request(const SubscribeRequestPayload& req) {
        subscription_protocol_.handle_subscribe_request(req, *this);
    }
    
    void add_subscriber(uint32_t subscriber_base_addr) {
        this->MultiOutputManager<SingleInputMultiOutputBase, UserRegistry, OutputTypesTuple>::add_subscriber_to_output(subscriber_base_addr);
    }
    
protected:
    template<typename... Ts>
    void publish_multi_outputs_with_timestamp(std::tuple<Ts...>& outputs, uint64_t timestamp_ns) {
        publish_multi_outputs_impl(outputs, timestamp_ns, std::index_sequence_for<Ts...>{});
    }
    
private:
    template<typename TupleType, std::size_t... Is>
    void publish_multi_outputs_impl(TupleType& outputs, uint64_t timestamp_ns, std::index_sequence<Is...>) {
        (publish_single_output<Is>(std::get<Is>(outputs), timestamp_ns), ...);
    }
    
    template<std::size_t Index, typename T>
    void publish_single_output(T& data, uint64_t timestamp_ns) {
        TimsMessage<T> tims_msg;
        tims_msg.header.timestamp = timestamp_ns;
        tims_msg.header.seq_number = 0;
        tims_msg.header.msg_type = UserRegistry::template get_message_id<T>();
        tims_msg.payload = data;
        
        auto& subscribers = this->template get_output_subscribers<Index>();
        auto serialized = sertial::Message<TimsMessage<T>>::serialize(tims_msg);
        if (!serialized) return;
        
        auto& publish_mbx = get_publish_mailbox<Index>();
        for (uint32_t subscriber_addr : subscribers) {
            publish_mbx.send_bytes(serialized.view(), subscriber_addr + 32);
        }
    }
};

} // namespace commrat
