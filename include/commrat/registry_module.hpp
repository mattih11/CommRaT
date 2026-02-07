#pragma once

#include "commrat/registry_mailbox.hpp"
#include "commrat/subscription_messages.hpp"
#include "commrat/system_registry.hpp"
#include "commrat/io_spec.hpp"
#include <sertial/sertial.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <functional>
#include <optional>
#include <mutex>

namespace commrat {

// ============================================================================
// Input Mode Tags
// ============================================================================

/// Periodic execution - module runs on a timer, no input data
struct PeriodicInput {
    std::chrono::milliseconds period{100};
};

/// Free-running loop - module runs as fast as possible, no input data
struct LoopInput {};

/// Continuous input - module receives data of type T from subscribed source
template<typename T>
struct ContinuousInput {
    using InputData = T;
    std::chrono::milliseconds requested_period{0};  // 0 = as fast as source publishes
};

// ============================================================================
// Module Configuration
// ============================================================================

// Mailbox type identifiers (offsets from base address)
enum class MailboxType : uint8_t {
    CMD = 0,   // Command mailbox - receives imperative commands
    WORK = 16,  // Work mailbox - handles subscription protocol  
    DATA = 32   // Data mailbox - receives input data streams
};

struct ModuleConfig {
    std::string name;
    uint8_t system_id{0};        // System/robot identifier
    uint8_t instance_id{0};      // Instance number for this module type
    std::chrono::milliseconds period{100};
    size_t message_slots{10};
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};
    
    // For ContinuousInput mode: source module's system_id and instance_id
    std::optional<uint8_t> source_system_id;
    std::optional<uint8_t> source_instance_id;
};

// ============================================================================
// Helper Base Classes for Conditional Virtual Functions
// ============================================================================

// Base class providing process_continuous when InputData is not void
template<typename InputData_, typename OutputData_>
class ContinuousProcessorBase {
protected:
    virtual OutputData_ process_continuous(const InputData_& input) {
        std::cerr << "[Module] ERROR: process_continuous not overridden in derived class!\n";
        return OutputData_{};
    }
};

// Specialization for void InputData (no process_continuous function)
template<typename OutputData_>
class ContinuousProcessorBase<void, OutputData_> {
    // No process_continuous for periodic/loop modes
};

// Helper to extract InputData from InputSpec (outside Module class)
template<typename T>
struct ExtractInputPayload {
    using type = void;  // PeriodicInput, LoopInput
};

template<typename T>
struct ExtractInputPayload<Input<T>> {
    using type = T;
};

template<typename T>
struct ExtractInputPayload<ContinuousInput<T>> {
    using type = T;  // Legacy support
};

// Helper to extract OutputData from OutputSpec (outside Module class)
template<typename T>
struct ExtractOutputPayload {
    using type = T;  // Raw type
};

template<typename T>
struct ExtractOutputPayload<Output<T>> {
    using type = T;
};

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
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class Module : public ContinuousProcessorBase<
    typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type,
    typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type
> {
private:
    // Normalize OutputSpec: raw type T -> Output<T>
    using OutputSpec = NormalizeOutput_t<OutputSpec_>;
    
    // Normalize InputSpec: ContinuousInput<T> -> Input<T>, keep others as-is
    using InputSpec = NormalizeInput_t<InputSpec_>;
    
    // Phase 5 constraint: reject multi-input/multi-output
    static_assert(OutputCount_v<OutputSpec> <= 1,
                  "Phase 5: Multi-output (Outputs<Ts...>) not yet supported. Use Output<T> or raw type.");
    static_assert(InputCount_v<InputSpec> <= 1,
                  "Phase 5: Multi-input (Inputs<Ts...>) not yet supported. Use Input<T>, PeriodicInput, or LoopInput.");
    
    // Helper to extract InputData type from InputSpec
    template<typename T>
    struct ExtractInputData {
        using type = void;
    };
    
    template<typename T>
    struct ExtractInputData<Input<T>> {
        using type = T;
    };
    
    // Helper to extract OutputData type from OutputSpec
    template<typename T>
    struct ExtractOutputData {
        using type = T;  // Raw type passes through
    };
    
    template<typename T>
    struct ExtractOutputData<Output<T>> {
        using type = T;
    };
    
    template<typename T>
    struct ExtractOutputData<Outputs<T>> {
        using type = void;  // Multi-output not yet supported
    };
    
public:
    using OutputData = typename ExtractOutputData<OutputSpec>::type;
    using InputData = typename ExtractInputData<InputSpec>::type;
    
    static constexpr bool has_continuous_input = HasContinuousInput<InputSpec>;
    static constexpr bool has_periodic_input = std::is_same_v<InputSpec, PeriodicInput>;
    static constexpr bool has_loop_input = std::is_same_v<InputSpec, LoopInput>;
    
private:
    // Type aliases for mailboxes
    using CmdMailbox = RegistryMailbox<UserRegistry>;  // Command mailbox (user commands only)
    using WorkMailbox = RegistryMailbox<SystemRegistry>;  // Work mailbox (subscription protocol)
    using DataMailbox = RegistryMailbox<UserRegistry>;  // Data mailbox (input data)
    
    /**
     * @brief Calculate base mailbox address from output type, system_id, and instance_id
     * Format: [data_type_id_low16:16][system_id:8][instance_id:8]
     * Uses lower 16 bits of message ID to fit in uint32_t
     */
    static constexpr uint32_t calculate_base_address(uint8_t system_id, uint8_t instance_id) {
        // Get message ID for output data type from registry
        constexpr uint32_t data_type_id = UserRegistry::template get_message_id<OutputData>();
        // Use lower 16 bits of message ID to fit addressing in 32 bits
        constexpr uint16_t data_type_id_low = static_cast<uint16_t>(data_type_id & 0xFFFF);
        return (static_cast<uint32_t>(data_type_id_low) << 16) | (system_id << 8) | instance_id;
    }
    
    /**
     * @brief Get specific mailbox address
     */
    static constexpr uint32_t get_mailbox_address(uint8_t system_id, uint8_t instance_id, MailboxType type) {
        return calculate_base_address(system_id, instance_id) + static_cast<uint8_t>(type);
    }
    
protected:
    ModuleConfig config_;
    CmdMailbox cmd_mailbox_;    // base + 0: Receives user commands
    WorkMailbox work_mailbox_;  // base + 1: Handles subscription protocol
    std::optional<DataMailbox> data_mailbox_;  // base + 2: Receives input data (only for ContinuousInput)
    
    std::atomic<bool> running_;
    
    // Module threads
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    std::optional<std::thread> work_thread_;
    
    // Subscriber management
    std::vector<uint32_t> subscribers_;
    std::mutex subscribers_mutex_;
    
public:
    explicit Module(const ModuleConfig& config)
        : config_(config)
        , cmd_mailbox_(createMailboxConfig(config, MailboxType::CMD))
        , work_mailbox_(createWorkMailboxConfig(config))
        , data_mailbox_(has_continuous_input ? 
            std::make_optional<DataMailbox>(createMailboxConfig(config, MailboxType::DATA)) : 
            std::nullopt)
        , running_(false)
    {
        subscribers_.reserve(config.max_subscribers);
    }
    
    virtual ~Module() {
        stop();
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
    
    // For PeriodicInput and LoopInput: return output data
    virtual OutputData process() {
        return OutputData{};
    }

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
    // Lifecycle Management
    // ========================================================================
    
    void start() {
        if (running_) {
            return;
        }
        
        on_init();
        
        // Start all 3 mailboxes
        auto cmd_result = cmd_mailbox_.start();
        if (!cmd_result) {
            throw std::runtime_error("Failed to start command mailbox");
        }
        
        auto work_result = work_mailbox_.start();
        if (!work_result) {
            throw std::runtime_error("Failed to start work mailbox");
        }
        
        // Only start data mailbox for ContinuousInput modules
        if (data_mailbox_) {
            auto data_result = data_mailbox_->start();
            if (!data_result) {
                throw std::runtime_error("Failed to start data mailbox");
            }
        }
        
        running_ = true;
        on_start();
        
        // Start work thread FIRST to handle subscriptions
        work_thread_ = std::thread(&Module::work_loop, this);
        
        // Start command thread for user commands
        command_thread_ = std::thread(&Module::command_loop, this);
        
        // Give threads time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Subscribe to source if continuous input
        if constexpr (has_continuous_input) {
            if (config_.source_system_id && config_.source_instance_id) {
                subscribe_to_source(*config_.source_system_id, *config_.source_instance_id);
            }
        }
        
        // Start data thread based on input mode
        if constexpr (has_periodic_input) {
            data_thread_ = std::thread(&Module::periodic_loop, this);
        } else if constexpr (has_loop_input) {
            data_thread_ = std::thread(&Module::free_loop, this);
        } else if constexpr (has_continuous_input) {
            data_thread_ = std::thread(&Module::continuous_loop, this);
        }
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        on_stop();
        
        // Unsubscribe from source
        if constexpr (has_continuous_input) {
            if (config_.source_system_id && config_.source_instance_id) {
                unsubscribe_from_source(*config_.source_system_id, *config_.source_instance_id);
            }
        }
        
        running_ = false;
        
        // Wait for threads to finish
        if (data_thread_ && data_thread_->joinable()) {
            data_thread_->join();
        }
        if (work_thread_ && work_thread_->joinable()) {
            work_thread_->join();
        }
        if (command_thread_ && command_thread_->joinable()) {
            command_thread_->join();
        }
        
        // Stop all mailboxes
        cmd_mailbox_.stop();
        work_mailbox_.stop();
        if (data_mailbox_) {
            data_mailbox_->stop();
        }
        
        on_cleanup();
    }
    
    bool is_running() const { return running_; }
    
    // ========================================================================
    // Subscriber Management
    // ========================================================================
    
protected:
    void add_subscriber(uint32_t subscriber_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        
        // Check if already subscribed
        if (std::find(subscribers_.begin(), subscribers_.end(), subscriber_id) != subscribers_.end()) {
            return;
        }
        
        // Check capacity
        if (subscribers_.size() >= config_.max_subscribers) {
            throw std::runtime_error("Maximum subscribers reached");
        }
        
        subscribers_.push_back(subscriber_id);
    }
    
    void remove_subscriber(uint32_t subscriber_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), subscriber_id),
            subscribers_.end()
        );
    }
    
    void publish_to_subscribers(OutputData& data) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        for (uint32_t subscriber_base_addr : subscribers_) {
            // Send to subscriber's DATA mailbox (base + 2)
            uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
            auto result = cmd_mailbox_.send(data, subscriber_data_mbx);  // Can send from any mailbox
            if (!result) {
                std::cout << "[" << config_.name << "] Send failed to subscriber " << subscriber_base_addr << "\n";
            }
        }
    }
    
    // ========================================================================
    // Subscription Protocol (ContinuousInput)
    // ========================================================================
    
    void subscribe_to_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "subscribe_to_source() only available for continuous input modules");
        
        // Calculate our base address to include in request
        uint32_t our_base_addr = calculate_base_address(config_.system_id, config_.instance_id);
        
        SubscribeRequestType request{
            .subscriber_mailbox_id = our_base_addr,  // Send our base address, not specific mailbox
            .requested_period_ms = config_.period.count()
        };
        
        // Calculate source module's WORK mailbox address
        // We need the source's output type ID, which we get from InputData type
        uint32_t source_base;
        uint32_t source_work_mbx;
        
        if constexpr (has_continuous_input) {
            constexpr uint32_t source_data_type_id = UserRegistry::template get_message_id<InputData>();
            constexpr uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
            source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | (source_system_id << 8) | source_instance_id;
            source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        }
        
        std::cout << "[" << config_.name << "] Sending SubscribeRequest to source WORK mailbox " 
                  << source_work_mbx << "\n";
        
        // Retry a few times in case the producer's mailbox isn't ready yet
        int max_retries = 5;
        for (int i = 0; i < max_retries; ++i) {
            // Send subscribe request from work mailbox (SystemRegistry messages)
            auto result = work_mailbox_.send(request, source_work_mbx);
            if (result) {
                std::cout << "[" << config_.name << "] SubscribeRequest sent successfully\n";
                return;
            }
            
            if (i < max_retries - 1) {
                std::cout << "[" << config_.name << "] Failed to send SubscribeRequest (attempt " 
                          << (i + 1) << "/" << max_retries << "), retrying...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        std::cout << "[" << config_.name << "] Failed to send SubscribeRequest after " 
                  << max_retries << " attempts!\n";
    }
    
    void unsubscribe_from_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "unsubscribe_from_source() only available for continuous input modules");
        
        uint32_t our_base_addr = calculate_base_address(config_.system_id, config_.instance_id);
        
        UnsubscribeRequestType request{
            .subscriber_mailbox_id = our_base_addr
        };
        
        // Calculate source WORK mailbox
        uint32_t source_base;
        uint32_t source_work_mbx;
        
        if constexpr (has_continuous_input) {
            constexpr uint32_t source_data_type_id = UserRegistry::template get_message_id<InputData>();
            constexpr uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
            source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | (source_system_id << 8) | source_instance_id;
            source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        }
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_.send(request, source_work_mbx);
    }
    
    void handle_subscribe_request(const SubscribeRequestType& req) {
        try {
            // req.subscriber_mailbox_id is the subscriber's base address
            add_subscriber(req.subscriber_mailbox_id);
            std::cout << "[" << config_.name << "] Added subscriber: " << req.subscriber_mailbox_id 
                      << " (total: " << subscribers_.size() << ")\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = config_.period.count(),
                .success = true,
                .error_code = 0
            };
            
            // Send reply to subscriber's WORK mailbox (base + 1)
            uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
            work_mailbox_.send(reply, subscriber_work_mbx);
        } catch (...) {
            std::cout << "[" << config_.name << "] Failed to add subscriber: " << req.subscriber_mailbox_id << "\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = 0,
                .success = false,
                .error_code = 1  // Max subscribers exceeded
            };
            
            uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
            work_mailbox_.send(reply, subscriber_work_mbx);
        }
    }
    
    void handle_unsubscribe_request(const UnsubscribeRequestType& req) {
        remove_subscriber(req.subscriber_mailbox_id);
        
        UnsubscribeReplyType reply{.success = true};
        uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
        work_mailbox_.send(reply, subscriber_work_mbx);
    }
    
    // ========================================================================
    // Main Loops
    // ========================================================================
    
private:
    void periodic_loop() {
        while (running_) {
            auto output = process();
            publish_to_subscribers(output);
            std::this_thread::sleep_for(config_.period);
        }
    }
    
    void free_loop() {
        while (running_) {
            auto output = process();
            publish_to_subscribers(output);
        }
    }
    
    void continuous_loop() {
        std::cout << "[" << config_.name << "] continuous_loop started, waiting for data...\n";
        while (running_) {
            // BLOCKING receive on data mailbox - no timeout, waits for data
            auto result = data_mailbox_->template receive<InputData>();
            
            if (result) {
                auto output = process_continuous_dispatch(result->message);
                publish_to_subscribers(output);
            }
        }
        std::cout << "[" << config_.name << "] continuous_loop ended\n";
    }
    
    void command_loop() {
        std::cout << "[" << config_.name << "] command_loop started\n";
        while (running_) {
            // Use receive_any with visitor pattern on cmd_mailbox
            // BLOCKING receive - waits indefinitely for user commands
            auto visitor = [this](auto&& received_msg) {
                // received_msg is ReceivedMessage<PayloadT>, extract payload
                auto& msg = received_msg.message;
                using MsgType = std::decay_t<decltype(msg)>;
                
                std::cout << "[" << config_.name << "] Received command in command_loop\n";
                
                // Handle user command types only
                handle_user_command(msg);
            };
            
            // BLOCKING receive on command mailbox (no timeout)
            cmd_mailbox_.receive_any(visitor);
        }
        std::cout << "[" << config_.name << "] command_loop ended\n";
    }
    
    void work_loop() {
        std::cout << "[" << config_.name << "] work_loop started, listening on WORK mailbox " 
                  << get_mailbox_address(config_.system_id, config_.instance_id, MailboxType::WORK) 
                  << "\n" << std::flush;
        while (running_) {
            // Use receive_any with visitor pattern on work_mailbox
            // BLOCKING receive - waits indefinitely for subscription messages
            std::cout << "[" << config_.name << "] work_loop: waiting for message...\n" << std::flush;
            auto visitor = [this](auto&& received_msg) {
                // received_msg is ReceivedMessage<PayloadT>, extract payload
                auto& msg = received_msg.message;
                using MsgType = std::decay_t<decltype(msg)>;
                
                // Handle subscription protocol
                if constexpr (std::is_same_v<MsgType, SubscribeRequestType>) {
                    std::cout << "[" << config_.name << "] Handling SubscribeRequest\n";
                    handle_subscribe_request(msg);
                } else if constexpr (std::is_same_v<MsgType, UnsubscribeRequestType>) {
                    std::cout << "[" << config_.name << "] Handling UnsubscribeRequest\n";
                    handle_unsubscribe_request(msg);
                }
            };
            
            // BLOCKING receive on work mailbox (no timeout)
            work_mailbox_.receive_any(visitor);
        }
        std::cout << "[" << config_.name << "] work_loop ended\n";
    }
    
    /**
     * @brief Dispatch user command to on_command handler
     */
    template<typename CmdT>
    void handle_user_command(const CmdT& cmd) {
        // Check if this is one of our declared CommandTypes
        if constexpr ((std::is_same_v<CmdT, CommandTypes> || ...)) {
            on_command(cmd);
        }
        // Otherwise ignore (not in our command list)
    }
    
    /**
     * @brief Handle a specific command type (override in derived class)
     * 
     * @tparam CmdT Command payload type
     * @param cmd Command payload
     */
    template<typename CmdT>
    void on_command(const CmdT& cmd) {
        // Default: no-op - override in derived classes for specific CommandTypes
    }
    
    // ========================================================================
    // Helper Functions
    // ========================================================================
    
    static MailboxConfig createMailboxConfig(const ModuleConfig& config, MailboxType type) {
        uint32_t mailbox_id = get_mailbox_address(config.system_id, config.instance_id, type);
        return MailboxConfig{
            .mailbox_id = mailbox_id,
            .message_slots = config.message_slots,
            .max_message_size = UserRegistry::max_message_size,
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name + "_" + 
                           (type == MailboxType::CMD ? "cmd" : 
                            type == MailboxType::WORK ? "work" : "data")
        };
    }
    
    static MailboxConfig createWorkMailboxConfig(const ModuleConfig& config) {
        uint32_t mailbox_id = get_mailbox_address(config.system_id, config.instance_id, MailboxType::WORK);
        return MailboxConfig{
            .mailbox_id = mailbox_id,
            .message_slots = config.message_slots,
            .max_message_size = SystemRegistry::max_message_size,  // Smaller buffer for system messages
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name + "_work"
        };
    }
};

} // namespace commrat
