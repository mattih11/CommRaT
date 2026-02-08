#pragma once

#include "commrat/mailbox.hpp"
#include "commrat/messages.hpp"
#include "commrat/subscription_messages.hpp"
#include "commrat/threading.hpp"
#include "commrat/timestamp.hpp"
#include <sertial/sertial.hpp>
#include <atomic>
#include <vector>
#include <functional>
#include <optional>

namespace commrat {

// ============================================================================
// Input Mode Tags
// ============================================================================

/// Periodic execution - module runs on a timer, no input data
struct PeriodicInput {
    std::chrono::milliseconds period{100};  // Default 100ms
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

struct ModuleConfig {
    std::string name;
    uint32_t mailbox_id;
    std::chrono::milliseconds period{100};  // For PeriodicInput
    size_t message_slots{10};  // Number of message slots in mailbox
    size_t max_subscribers{8};  // Reserved capacity for subscriber list
    int priority{10};
    bool realtime{false};
    
    // For ContinuousInput mode: source module's mailbox ID
    std::optional<uint32_t> source_mailbox_id;  // Required for ContinuousInput
};

// ============================================================================
// Module Base Template
// ============================================================================

/**
 * @brief Modern C++20 module base with type-safe mailbox communication
 * 
 * @tparam OutputDataT The data type this module produces
 * @tparam InputModeT Input mode: PeriodicInput, LoopInput, or ContinuousInput<T>
 * @tparam CommandTypes... Commands this module handles (optional)
 * 
 * Design principles:
 * - Zero runtime allocation in hot paths
 * - All type dispatch at compile time (if constexpr)
 * - Direct mailbox usage (no proxy wrapper)
 * - RAII lifecycle management
 * - SeRTial integration for serialization
 */
template<typename OutputDataT_,
         typename InputModeT = PeriodicInput,
         typename... CommandTypes>
class Module {
private:
    // Helper to extract InputData type from InputMode (SFINAE-friendly)
    template<typename T, typename = void>
    struct ExtractInputData {
        using type = void;
    };
    
    template<typename T>
    struct ExtractInputData<T, std::void_t<typename T::InputData>> {
        using type = typename T::InputData;
    };
    
public:
    // Type aliases for introspection
    using OutputDataT = OutputDataT_;  // Expose template param with different name
    using OutputData = OutputDataT_;
    using InputMode = InputModeT;
    
    // Determine if this module has continuous input
    static constexpr bool has_continuous_input = requires (InputMode m) { 
        typename InputMode::InputData;
    };
    
    // Determine if this module has periodic input
    static constexpr bool has_periodic_input = std::is_same_v<InputMode, PeriodicInput>;
    
    // Extract input data type (void if no continuous input)
    using InputData = typename ExtractInputData<InputMode>::type;
    using InputDataT = InputData;  // Alias for consistency
    
    // Build message registry for this module
    // Include OutputData, InputData (if continuous), subscription messages, and all command types
    using OutputMessages = MessageRegistry<OutputData>;
    
    using InputMessages = std::conditional_t<
        has_continuous_input && !std::is_void_v<InputData>,
        MessageRegistry<InputData>,
        MessageRegistry<>  // Empty registry for non-continuous
    >;
    
    // Build mailbox type list conditionally - using MessageDefinition types
    using Mailbox = std::conditional_t<
        has_continuous_input,
        commrat::Mailbox<
            MessageDefinition<OutputData, MessagePrefix::UserDefined, UserSubPrefix::Data>,
            MessageDefinition<InputData, MessagePrefix::UserDefined, UserSubPrefix::Data>,
            SubscribeRequest,      // Already MessageDefinition
            SubscribeReply,        // Already MessageDefinition
            UnsubscribeRequest,    // Already MessageDefinition
            UnsubscribeReply,      // Already MessageDefinition
            MessageDefinition<CommandTypes, MessagePrefix::UserDefined, UserSubPrefix::Commands>...
        >,
        commrat::Mailbox<
            MessageDefinition<OutputData, MessagePrefix::UserDefined, UserSubPrefix::Data>,
            SubscribeRequest,
            SubscribeReply,
            UnsubscribeRequest,
            UnsubscribeReply,
            MessageDefinition<CommandTypes, MessagePrefix::UserDefined, UserSubPrefix::Commands>...
        >
    >;

public:
    /**
     * @brief Construct module with configuration
     */
    explicit Module(const ModuleConfig& config)
        : config_(config)
        , mailbox_(createMailboxConfig(config))
        , running_(false)
    {
        // Reserve space for configured subscriber count to avoid reallocation
        subscribers_.reserve(config.max_subscribers);
    }
    
    virtual ~Module() {
        stop();
    }
    
    // Prevent copy/move
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;

protected:
    // ========================================================================
    // Lifecycle Hooks - Override in derived classes
    // ========================================================================
    
    /// Called once during initialization before threads start
    virtual void on_init() {}
    
    /// Called when module starts (after threads created)
    virtual void on_start() {}
    
    /// Called when module stops (before threads join)
    virtual void on_stop() {}
    
    /// Called during cleanup after threads stopped
    virtual void on_cleanup() {}
    
    // ========================================================================
    // Processing Loop - Override based on input mode
    // ========================================================================
    
    /// For PeriodicInput and LoopInput modes
    virtual void process(OutputDataT_& output) {
        (void)output;
        // Default: do nothing
    }
    
    // ========================================================================
    // ContinuousInput Process - SFINAE-enabled only when InputData is not void
    // ========================================================================
private:
    // Internal dispatch helper
    template<typename T = InputData, typename = std::enable_if_t<!std::is_void_v<T>>>
    void process_continuous_internal(const T& input, OutputDataT_& output) {
        // Call derived class implementation
        process_continuous(input, output);
    }

protected:
    /// Override this in derived classes for ContinuousInput mode
    template<typename T = InputData>
    std::enable_if_t<!std::is_void_v<T>> process_continuous(const T& input, OutputDataT_& output) {
        (void)input;
        (void)output;
        // Default: do nothing
    }
    
    // ========================================================================
    // Command Handling - Override to handle commands
    // ========================================================================
    
    /// Override to handle commands (called from command thread)
    /// Use if constexpr to check command types at compile time
    template<typename CmdT>
    void on_command(const CmdT& cmd) {
        (void)cmd;
        // Default: ignore unknown commands
    }

public:
    // ========================================================================
    // Public Interface
    // ========================================================================
    
    /**
     * @brief Start module operation
     * Starts mailbox, calls on_init(), spawns threads, calls on_start()
     */
    void start() {
        if (running_.exchange(true)) {
            return;  // Already running
        }
        
        // Start mailbox
        auto result = mailbox_.start();
        if (!result) {
            running_ = false;
            throw std::runtime_error("Failed to start mailbox: " + config_.name);
        }
        
        // Initialize
        on_init();
        
        // Start processing thread based on input mode
        if constexpr (std::is_same_v<InputMode, PeriodicInput>) {
            processing_thread_ = std::thread([this]{ periodic_loop(); });
        } else if constexpr (std::is_same_v<InputMode, LoopInput>) {
            processing_thread_ = std::thread([this]{ free_loop(); });
        } else if constexpr (has_continuous_input) {
            processing_thread_ = std::thread([this]{ continuous_loop(); });
        }
        
        // Start command thread
        command_thread_ = std::thread([this]{ command_loop(); });
        
        // Auto-subscribe for continuous input mode
        if constexpr (has_continuous_input) {
            subscribe_to_source();
        }
        
        on_start();
    }
    
    /**
     * @brief Stop module operation
     * Calls on_stop(), stops threads, stops mailbox, calls on_cleanup()
     */
    void stop() {
        if (!running_.exchange(false)) {
            return;  // Already stopped
        }
        
        on_stop();
        
        // Auto-unsubscribe for continuous input mode
        if constexpr (has_continuous_input) {
            unsubscribe_from_source();
        }
        
        // Wait for threads to finish
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }
        if (command_thread_.joinable()) {
            command_thread_.join();
        }
        
        // Stop mailbox
        mailbox_.stop();
        
        on_cleanup();
    }
    
    /**
     * @brief Check if module is running
     */
    bool is_running() const {
        return running_.load();
    }
    
    /**
     * @brief Get module mailbox ID
     */
    uint32_t mailbox_id() const {
        return config_.mailbox_id;
    }
    
    /**
     * @brief Get module name
     */
    const std::string& name() const {
        return config_.name;
    }
    
    /**
     * @brief Add subscriber to receive this module's output
     * @return true if added, false if max_subscribers limit reached
     */
    bool add_subscriber(uint32_t dest_mailbox_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        
        // Check if already subscribed
        if (std::find(subscribers_.begin(), subscribers_.end(), dest_mailbox_id) != subscribers_.end()) {
            return true;  // Already subscribed
        }
        
        // Check capacity limit
        if (subscribers_.size() >= config_.max_subscribers) {
            return false;  // Max subscribers reached
        }
        
        subscribers_.push_back(dest_mailbox_id);
        return true;
    }
    
    /**
     * @brief Remove subscriber
     */
    void remove_subscriber(uint32_t dest_mailbox_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), dest_mailbox_id),
            subscribers_.end()
        );
    }
    
    /**
     * @brief Publish output data to all subscribers
     */
    void publish(const OutputDataT_& data) {
        // Create TimsMessage wrapper
        OutputMessage message;
        message.payload = data;
        
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        for (uint32_t dest_id : subscribers_) {
            auto result = mailbox_.send(message, dest_id);
            if (!result) {
                // Log error but continue
            }
        }
    }

    /**
     * @brief Access to configuration
     */
    const ModuleConfig& config() const { return config_; }

protected:
    /**
     * @brief Access to mailbox for advanced use cases
     */
    Mailbox& mailbox() { return mailbox_; }
    const Mailbox& mailbox() const { return mailbox_; }

private:
    // ========================================================================
    // Internal Thread Loops
    // ========================================================================
    
    /// Periodic execution loop
    void periodic_loop() {
        while (running_.load()) {
            auto start_time = std::chrono::steady_clock::now();
            
            // Create output buffer
            OutputDataT_ output{};
            
            // Process
            process(output);
            
            // Publish to subscribers
            publish(output);
            
            // Sleep to maintain period
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto sleep_time = config_.period - 
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
            
            if (sleep_time > std::chrono::milliseconds(0)) {
                Time::sleep(sleep_time);
            }
        }
    }
    
    /// Free-running loop
    void free_loop() {
        while (running_.load()) {
            OutputDataT_ output{};
            process(output);
            publish(output);
            // No sleep - runs as fast as possible
        }
    }
    
    /// Continuous input loop
    void continuous_loop() {
        while (running_.load()) {
            // Wait for input data from subscribed source
            auto input_result = mailbox_.template receive_for<TimsMessage<InputData>>(
                std::chrono::milliseconds(1000)  // 1 second timeout
            );
            
            if (!input_result) {
                // Timeout or error - continue waiting
                continue;
            }
            
            // Create output buffer
            OutputDataT_ output{};
            
            // Process input -> output (access payload)
            process_continuous_internal(input_result->message.payload, output);
            
            // Publish to subscribers
            publish(output);
        }
    }
    
    /// Command processing loop with compile-time dispatch
    void command_loop() {
        while (running_.load()) {
            // Use visitor pattern to receive any command message
            auto result = mailbox_.receive_any([this](auto&& received_msg) {
                // received_msg is ReceivedMessage<TimsMessage<T>>
                // Access the payload directly
                this->handle_command(received_msg.message.payload, 0);  // TODO: get real sender_id
            });
            
            // receive_any blocks for 1 second by default
            if (!result) {
                // Timeout or error - continue
                continue;
            }
        }
    }
    
    /// Subscribe to source for continuous input
    void subscribe_to_source() {
        if constexpr (has_continuous_input) {
            if (!config_.source_mailbox_id) {
                throw std::runtime_error("ContinuousInput mode requires source_mailbox_id in config");
            }
            
            // Create subscribe request using payload type
            TimsMessage<SubscribeRequestPayload> request;
            request.payload.subscriber_mailbox_id = config_.mailbox_id;
            request.payload.requested_period_ms = InputMode{}.requested_period.count();  // Convert to milliseconds
            
            std::cout << "[DEBUG] Sending SubscribeRequest from " << config_.mailbox_id 
                      << " to " << *config_.source_mailbox_id << std::endl;
            std::cout << "[DEBUG] Mailbox running: " << mailbox_.is_running() << std::endl;
            
            // Send subscribe request
            auto send_result = mailbox_.send(request, *config_.source_mailbox_id);
            if (!send_result) {
                std::cerr << "[ERROR] Send failed" << std::endl;
                throw std::runtime_error("Failed to send SubscribeRequest");
            }
            
            std::cout << "[DEBUG] SubscribeRequest sent successfully, waiting for reply..." << std::endl;
            
            // Wait for reply
            auto reply = mailbox_.template receive_for<TimsMessage<SubscribeReplyPayload>>(std::chrono::milliseconds(2000));
            if (!reply) {
                throw std::runtime_error("No SubscribeReply received from source");
            }
            
            if (!reply->message.payload.success) {
                throw std::runtime_error("Subscription rejected by source, error code: " + 
                                       std::to_string(reply->message.payload.error_code));
            }
            
            // Success - source will now push data to us
        }
    }
    
    /// Unsubscribe from source
    void unsubscribe_from_source() {
        if constexpr (has_continuous_input) {
            if (!config_.source_mailbox_id) {
                return;  // Nothing to unsubscribe from
            }
            
            TimsMessage<UnsubscribeRequestPayload> request;
            request.payload.subscriber_mailbox_id = config_.mailbox_id;
            
            // Fire and forget - don't wait for reply during shutdown
            mailbox_.send(request, *config_.source_mailbox_id);
        }
    }
    
    /// Generic command handler dispatcher - routes to specific handlers
    template<typename CmdT>
    void handle_command(const CmdT& cmd, uint32_t sender_id) {
        if constexpr (std::is_same_v<CmdT, SubscribeRequest>) {
            handle_subscribe(cmd, sender_id);
        } else if constexpr (std::is_same_v<CmdT, UnsubscribeRequest>) {
            handle_unsubscribe(cmd, sender_id);
        } else {
            // User-defined command - call virtual handler
            on_command(cmd, sender_id);
        }
    }
    
    /// Handle incoming subscribe request
    void handle_subscribe(const SubscribeRequestPayload& request, uint32_t sender_id) {
        TimsMessage<SubscribeReplyPayload> reply;
        
        // Attempt to add subscriber
        if (add_subscriber(request.subscriber_mailbox_id)) {
            reply.payload.success = true;
            reply.payload.actual_period_ms = get_output_period();  // Inform subscriber of our publish rate
            reply.payload.error_code = 0;
        } else {
            reply.payload.success = false;
            reply.payload.actual_period_ms = 0;
            reply.payload.error_code = 1;  // Subscriber limit reached
        }
        
        // Send reply back to requester
        mailbox_.send(reply, sender_id);
    }
    
    /// Handle incoming unsubscribe request
    void handle_unsubscribe(const UnsubscribeRequestPayload& request, uint32_t sender_id) {
        // Remove subscriber
        remove_subscriber(request.subscriber_mailbox_id);
        
        // Send reply
        TimsMessage<UnsubscribeReplyPayload> reply;
        reply.payload.success = true;
        mailbox_.send(reply, sender_id);
    }
    
    /// Get the output period for this module in milliseconds
    int64_t get_output_period() const {
        if constexpr (has_periodic_input) {
            return InputMode{}.period.count();
        } else {
            return 0;  // Not periodic
        }
    }
    
    /// User-defined typed command handler (override to handle custom CommandTypes)
    template<typename CmdT>
    void on_command(const CmdT&, uint32_t) {
        // Default: do nothing - derived classes override for specific command types
    }
    
    /// Create MailboxConfig from module config
    MailboxConfig createMailboxConfig(const ModuleConfig& config) {
        return MailboxConfig{
            .mailbox_id = config.mailbox_id,
            .message_slots = config.message_slots,
            .max_message_size = OutputMessages::max_message_size,  // Compile-time from SeRTial
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name
        };
    }
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    ModuleConfig config_;
    Mailbox mailbox_;
    
    std::atomic<bool> running_;
    std::thread processing_thread_;
    std::thread command_thread_;
    
    std::vector<uint32_t> subscribers_;
    Mutex subscribers_mutex_;
};

} // namespace commrat
