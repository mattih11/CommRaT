#pragma once

#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/platform/threading.hpp"
#include "commrat/platform/timestamp.hpp"
#include "commrat/module/module_core.hpp"
#include "commrat/module/module_mixins.hpp"
#include "commrat/module/module_services.hpp"

#include <sertial/sertial.hpp>
#include <atomic>
#include <optional>

namespace commrat {

/**
 * @brief Module base for single-input single-output modules
 * 
 * Specialized implementation for modules that:
 * - Process incoming data (Input<T>)
 * - Produce single output type (Output<U> or raw U)
 * 
 * Provides virtual function signature:
 *   void process(const InputData& input, OutputData& output)
 * 
 * Execution model:
 * - Blocks on input mailbox waiting for messages
 * - Calls process() for each received message
 * - Publishes output to subscribers
 * - Propagates timestamp from input to output
 */
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class SingleInputSingleOutputBase
    // Only inherit what we need for this mode:
    : public SingleOutputProcessorBase<typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type, typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type>
    , public LifecycleManager<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public LoopExecutor<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public MailboxInfrastructureBuilder<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry>
    , public InputMetadataManager<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public InputMetadataAccessors<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public CommandDispatcher<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>
    , public MultiOutputManager<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>
    , public WorkLoopHandler<SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
{
    // Friend declarations for CRTP mixins
    friend class LifecycleManager<SingleInputSingleOutputBase>;
    friend class LoopExecutor<SingleInputSingleOutputBase>;
    friend class MailboxInfrastructureBuilder<SingleInputSingleOutputBase, UserRegistry>;
    friend class InputMetadataManager<SingleInputSingleOutputBase>;
    friend class InputMetadataAccessors<SingleInputSingleOutputBase>;
    friend class CommandDispatcher<SingleInputSingleOutputBase, CommandTypes...>;
    friend class MultiOutputManager<SingleInputSingleOutputBase, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>;
    friend class WorkLoopHandler<SingleInputSingleOutputBase>;
    
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
    using DataMailbox = typename ModuleTypes::DataMailbox;
    
    // Counts and flags
    static constexpr size_t num_output_types = ModuleTypes::num_output_types;
    static constexpr size_t InputCount = ModuleTypes::InputCount;
    
    // Compile-time validation
    static_assert(ModuleTypes::has_continuous_input, 
                  "SingleInputSingleOutputBase requires Input<T>");
    static_assert(!ModuleTypes::has_multi_output, "SingleInputSingleOutputBase requires single output");
    static_assert(!ModuleTypes::has_multi_input, "SingleInputSingleOutputBase does not support multi-input");
    static_assert(!ModuleTypes::has_periodic_input, "SingleInputSingleOutputBase does not support periodic input");
    static_assert(!ModuleTypes::has_loop_input, "SingleInputSingleOutputBase does not support loop input");
    
public:
    // Public type aliases (user-visible)
    using Registry = UserRegistry;
    using OutputData = typename ExtractOutputPayload<OutputSpec>::type;
    using InputData = typename ExtractInputPayload<InputSpec>::type;
    
    // Mode flags (for introspection)
    static constexpr bool has_periodic_input = false;
    static constexpr bool has_continuous_input = true;
    static constexpr bool has_loop_input = false;
    static constexpr bool has_multi_input = false;
    static constexpr bool has_multi_output = false;
    
protected:
    ModuleConfig config_;
    
    // Mailbox infrastructure
    MailboxSetTuple mailbox_infrastructure_;
    
    // Data mailbox for receiving input
    std::unique_ptr<DataMailbox> data_mailbox_;
    
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
        true,  // has_continuous_input
        false,  // has_multi_input
        InputData,
        InputTypesTuple,
        InputCount,
        OutputData,
        OutputTypesTuple
    >;
    SubscriptionProtocolType subscription_protocol_;
    
    // Publishing
    using PublisherType = commrat::Publisher<
        UserRegistry, 
        OutputData, 
        PublishMailbox,
        SingleInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>
    >;
    PublisherType publisher_;
    
    std::atomic<bool> running_;
    
    // Threads
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    
    // Input metadata
    static constexpr std::size_t num_inputs = 1;
    std::array<InputMetadataStorage, 1> input_metadata_;
    
    // Helper: Initialize mailbox infrastructure
    template<std::size_t... Is>
    void initialize_mailbox_infrastructure_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        ((std::get<Is>(mailbox_infrastructure_).initialize(config)), ...);
    }

public:
    explicit SingleInputSingleOutputBase(const ModuleConfig& config)
        : config_(config)
        , mailbox_infrastructure_{}
        , running_(false)
    {
        // Initialize mailbox infrastructure
        initialize_mailbox_infrastructure_impl(config, std::make_index_sequence<num_output_types>{});
        
        // Create data mailbox for receiving input
        uint32_t base_address = calculate_base_address(config.system_id, config.instance_id);
        data_mailbox_ = std::make_unique<DataMailbox>(
            UserRegistry::template get_message_id<InputData>(),
            base_address + 32,  // DATA mailbox offset
            config.message_slots
        );
        
        // Initialize per-output subscriber lists
        this->initialize_output_subscribers();
        
        // Initialize subscription protocol
        subscription_protocol_.set_config(&config_);
        subscription_protocol_.set_work_mailbox(&work_mailbox());
        subscription_protocol_.set_module_name(config.name);
        
        // Initialize publisher
        publisher_.set_module_ptr(this);
        publisher_.set_module_name(config.name);
    }
    
    virtual ~SingleInputSingleOutputBase() {
        this->stop();
    }
    
    SingleInputSingleOutputBase(const SingleInputSingleOutputBase&) = delete;
    SingleInputSingleOutputBase& operator=(const SingleInputSingleOutputBase&) = delete;
    SingleInputSingleOutputBase(SingleInputSingleOutputBase&&) = delete;
    SingleInputSingleOutputBase& operator=(SingleInputSingleOutputBase&&) = delete;
    
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
        this->MultiOutputManager<SingleInputSingleOutputBase, UserRegistry, OutputTypesTuple>::add_subscriber_to_output(subscriber_base_addr);
    }
    
    void remove_subscriber(uint32_t subscriber_base_addr) {
        this->MultiOutputManager<SingleInputSingleOutputBase, UserRegistry, OutputTypesTuple>::remove_subscriber(subscriber_base_addr);
    }
    
protected:
    // ========================================================================
    // Publishing
    // ========================================================================
    
    template<typename T = OutputData>
        requires (!std::is_void_v<T>)
    void publish_to_subscribers(T& data) {
        publisher_.publish_to_subscribers(data);
    }
    
    template<typename T>
    void publish_tims_message(TimsMessage<T>& tims_msg) {
        publisher_.publish_tims_message(tims_msg);
    }
    
    template<typename T>
    static TimsMessage<T> create_tims_message(T&& payload, uint64_t timestamp_ns) {
        return PublisherType::create_tims_message(std::forward<T>(payload), timestamp_ns);
    }
    
    // ========================================================================
    // Process Function (Override in derived class)
    // ========================================================================
    
    /**
     * @brief Process input and generate output
     * 
     * Called for each received input message. Timestamp is automatically
     * propagated from input to output.
     * 
     * @param input Const reference to input data
     * @param output Reference to output data to populate
     */
    virtual void process(const InputData& input, OutputData& output) = 0;
};

} // namespace commrat
