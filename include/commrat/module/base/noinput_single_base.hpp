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
 * @brief Module base for no-input single-output modules
 * 
 * Specialized implementation for modules that:
 * - Generate data without input (PeriodicInput or LoopInput)
 * - Produce single output type (Output<T> or raw T)
 * 
 * Provides virtual function signature:
 *   void process(OutputData& output)
 * 
 * Execution models:
 * - PeriodicInput: Runs at fixed period (config.period)
 * - LoopInput: Runs at maximum throughput (tight loop)
 * 
 * Both modes call process() to generate output and wrap in TimsMessage
 * with timestamp = Time::now()
 */
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class NoInputSingleOutputBase
    // Only inherit what we need for this mode:
    : public SingleOutputProcessorBase<void, typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type>
    , public LifecycleManager<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public LoopExecutor<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public MailboxInfrastructureBuilder<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry>
    , public InputMetadataManager<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public InputMetadataAccessors<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public CommandDispatcher<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>
    , public MultiOutputManager<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>
    , public WorkLoopHandler<NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
{
    // Friend declarations for CRTP mixins
    friend class LifecycleManager<NoInputSingleOutputBase>;
    friend class LoopExecutor<NoInputSingleOutputBase>;
    friend class MailboxInfrastructureBuilder<NoInputSingleOutputBase, UserRegistry>;
    friend class InputMetadataManager<NoInputSingleOutputBase>;
    friend class InputMetadataAccessors<NoInputSingleOutputBase>;
    friend class CommandDispatcher<NoInputSingleOutputBase, CommandTypes...>;
    friend class MultiOutputManager<NoInputSingleOutputBase, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>;
    friend class WorkLoopHandler<NoInputSingleOutputBase>;
    
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
                  "NoInputSingleOutputBase requires PeriodicInput or LoopInput");
    static_assert(!ModuleTypes::has_multi_output, "NoInputSingleOutputBase requires single output");
    static_assert(!ModuleTypes::has_multi_input, "NoInputSingleOutputBase does not support multi-input");
    static_assert(!ModuleTypes::has_continuous_input, "NoInputSingleOutputBase does not support continuous input");
    
public:
    // Public type aliases (user-visible)
    using OutputData = typename ModuleTypes::OutputData;
    using InputData = typename ModuleTypes::InputData;  // void for no-input modes
    
    // Input mode flags (dynamic based on actual InputSpec)
    static constexpr bool has_periodic_input = ModuleTypes::has_periodic_input;
    static constexpr bool has_continuous_input = ModuleTypes::has_continuous_input;
    static constexpr bool has_loop_input = ModuleTypes::has_loop_input;
    static constexpr bool has_multi_input = ModuleTypes::has_multi_input;
    static constexpr bool has_multi_output = ModuleTypes::has_multi_output;
    
protected:
    ModuleConfig config_;
    
    // Mailbox infrastructure
    MailboxSetTuple mailbox_infrastructure_;
    
    // Compatibility with LifecycleManager (expects data_mailbox_)
    // No-input modules don't use data mailbox - set to nullptr
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
        NoInputSingleOutputBase<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>
    >;
    PublisherType publisher_;
    
    std::atomic<bool> running_;
    
    // Threads
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    
    // Input metadata (unused for no-input modes, but required by InputMetadataManager)
    static constexpr std::size_t num_inputs = InputCount_v<InputSpec>;
    std::array<InputMetadataStorage, (num_inputs > 0 ? num_inputs : 1)> input_metadata_;
    
    // Helper: Initialize mailbox infrastructure
    template<std::size_t... Is>
    void initialize_mailbox_infrastructure_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        ((std::get<Is>(mailbox_infrastructure_).initialize(config)), ...);
    }

public:
    explicit NoInputSingleOutputBase(const ModuleConfig& config)
        : config_(config)
        , mailbox_infrastructure_{}
        , running_(false)
    {
        // Initialize mailbox infrastructure
        initialize_mailbox_infrastructure_impl(config, std::make_index_sequence<num_output_types>{});
        
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
    
    virtual ~NoInputSingleOutputBase() {
        this->stop();
    }
    
    NoInputSingleOutputBase(const NoInputSingleOutputBase&) = delete;
    NoInputSingleOutputBase& operator=(const NoInputSingleOutputBase&) = delete;
    NoInputSingleOutputBase(NoInputSingleOutputBase&&) = delete;
    NoInputSingleOutputBase& operator=(NoInputSingleOutputBase&&) = delete;
    
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
    
    void add_subscriber_to_output(uint32_t subscriber_base_addr) {
        this->MultiOutputManager<NoInputSingleOutputBase, UserRegistry, OutputTypesTuple>::add_subscriber_to_output(subscriber_base_addr);
    }
    
    void remove_subscriber(uint32_t subscriber_base_addr) {
        this->MultiOutputManager<NoInputSingleOutputBase, UserRegistry, OutputTypesTuple>::remove_subscriber(subscriber_base_addr);
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
};

} // namespace commrat
