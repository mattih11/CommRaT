#pragma once

#include "tims_wrapper.hpp"
#include "messages.hpp"
#include "message_registry.hpp"
#include <expected>
#include <optional>
#include <chrono>
#include <functional>
#include <concepts>
#include <span>
#include <mutex>
#include <queue>

namespace commrat {

// ============================================================================
// Error Types
// ============================================================================

enum class MailboxError {
    NotInitialized,
    InvalidMessage,
    Timeout,
    QueueFull,
    QueueEmpty,
    SerializationError,
    NetworkError,
    InvalidDestination,
    AlreadyRunning,
    NotRunning
};

// Convert error to string
constexpr const char* to_string(MailboxError error) {
    switch (error) {
        case MailboxError::NotInitialized:      return "Mailbox not initialized";
        case MailboxError::InvalidMessage:      return "Invalid message";
        case MailboxError::Timeout:             return "Operation timed out";
        case MailboxError::QueueFull:           return "Message queue is full";
        case MailboxError::QueueEmpty:          return "Message queue is empty";
        case MailboxError::SerializationError:  return "Serialization failed";
        case MailboxError::NetworkError:        return "Network error";
        case MailboxError::InvalidDestination:  return "Invalid destination mailbox";
        case MailboxError::AlreadyRunning:      return "Mailbox is already running";
        case MailboxError::NotRunning:          return "Mailbox is not running";
    }
    return "Unknown error";
}

// ============================================================================
// Result Type (C++20-compatible alternative to std::expected)
// ============================================================================

template<typename T>
class MailboxResult {
private:
    std::optional<T> value_;
    std::optional<MailboxError> error_;
    
public:
    // Success constructor
    MailboxResult(T value) : value_(std::move(value)), error_(std::nullopt) {}
    
    // Error constructor
    MailboxResult(MailboxError error) : value_(std::nullopt), error_(error) {}
    
    // Check if result contains a value
    explicit operator bool() const { return value_.has_value(); }
    bool has_value() const { return value_.has_value(); }
    
    // Access the value
    T& operator*() & { return *value_; }
    const T& operator*() const & { return *value_; }
    T&& operator*() && { return std::move(*value_); }
    
    T* operator->() { return &(*value_); }
    const T* operator->() const { return &(*value_); }
    
    T& value() & { return *value_; }
    const T& value() const & { return *value_; }
    T&& value() && { return std::move(*value_); }
    
    // Access the error
    MailboxError error() const { return *error_; }
};

// Specialization for void
template<>
class MailboxResult<void> {
private:
    std::optional<MailboxError> error_;
    
public:
    // Success constructor
    MailboxResult() : error_(std::nullopt) {}
    
    // Error constructor
    MailboxResult(MailboxError error) : error_(error) {}
    
    // Check if result is success
    explicit operator bool() const { return !error_.has_value(); }
    bool has_value() const { return !error_.has_value(); }
    
    // Access the error
    MailboxError error() const { return *error_; }
};

// ============================================================================
// Received Message Wrapper
// ============================================================================

template<typename T>
struct ReceivedMessage {
    T message;
    
    // Metadata from message header
    uint64_t timestamp;
    uint32_t sequence_number;
    
    // Accessor for payload
    const T& operator*() const { return message; }
    T& operator*() { return message; }
    const T* operator->() const { return &message; }
    T* operator->() { return &message; }
};

// ============================================================================
// Mailbox Configuration
// ============================================================================

struct MailboxConfig {
    uint32_t mailbox_id;
    size_t message_slots = 10;
    size_t max_message_size = 4096;
    uint8_t send_priority = 10;
    bool realtime = false;
    std::string mailbox_name = "";
};

// ============================================================================
// Mailbox Template Class
// ============================================================================

/**
 * @brief Strongly-typed mailbox for message-based communication
 * 
 * A modern C++20 mailbox interface that provides compile-time type safety
 * for message operations. Uses template parameter pack to statically define
 * which message types the mailbox can handle.
 * 
 * @tparam MessageTypes Pack of message types this mailbox can handle
 * 
 * Example:
 * @code
 * Mailbox<StatusMessage, CommandMessage> mbx(config);
 * mbx.start();
 * 
 * StatusMessage status;
 * status.payload.status_code = 100;
 * mbx.send(status, dest_mailbox_id);
 * 
 * auto result = mbx.receive<CommandMessage>();
 * if (result) {
 *     std::cout << "Command: " << result->message.payload.command_id << "\n";
 * }
 * @endcode
 */
template<typename... MessageTypes>
class Mailbox {
private:
    // Internal registry for this mailbox's supported types
    using Registry = MessageRegistry<MessageTypes...>;
    
    // Validate that all types are CommRaT messages
    static_assert((is_commrat_message_v<MessageTypes> && ...),
                  "All MessageTypes must be valid CommRaT message types");
    
public:
    // ========================================================================
    // Construction and Lifecycle
    // ========================================================================
    
    /**
     * @brief Construct a mailbox with the given configuration
     * @param config Mailbox configuration
     */
    explicit Mailbox(const MailboxConfig& config)
        : config_(config)
        , tims_(create_tims_config(config))
        , running_(false) {
    }
    
    /**
     * @brief Destructor - automatically stops and cleans up
     */
    ~Mailbox() {
        stop();
    }
    
    // Non-copyable
    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;
    
    // Movable
    Mailbox(Mailbox&& other) noexcept
        : config_(std::move(other.config_))
        , tims_(std::move(other.tims_))
        , running_(other.running_.load()) {
        other.running_ = false;
    }
    
    Mailbox& operator=(Mailbox&& other) noexcept {
        if (this != &other) {
            stop();
            config_ = std::move(other.config_);
            tims_ = std::move(other.tims_);
            running_ = other.running_.load();
            other.running_ = false;
        }
        return *this;
    }
    
    // ========================================================================
    // Lifecycle Management
    // ========================================================================
    
    /**
     * @brief Start the mailbox (initialize TiMS connection)
     * @return Success or error
     */
    auto start() -> MailboxResult<void> {
        if (running_) {
            return MailboxError::AlreadyRunning;
        }
        
        auto result = tims_.initialize();
        if (result != TimsResult::SUCCESS) {
            return MailboxError::NotInitialized;
        }
        
        running_ = true;
        return MailboxResult<void>();
    }
    
    /**
     * @brief Stop the mailbox
     */
    void stop() {
        if (running_) {
            running_ = false;
            tims_.shutdown();
        }
    }
    
    /**
     * @brief Check if mailbox is running
     */
    bool is_running() const {
        return running_;
    }
    
    // ========================================================================
    // Type Validation
    // ========================================================================
    
    /**
     * @brief Check if a type is registered with this mailbox at compile time
     */
    template<typename T>
    static constexpr bool is_registered = Registry::template is_registered<T>;
    
    /**
     * @brief Get number of registered message types
     */
    static constexpr size_t num_message_types() {
        return sizeof...(MessageTypes);
    }
    
    // ========================================================================
    // Send Operations
    // ========================================================================
    
    /**
     * @brief Send a message to a destination mailbox
     * 
     * @tparam T Message type (must be registered)
     * @param message Message to send
     * @param dest_mailbox Destination mailbox ID
     * @return Success or error
     */
    template<typename T>
        requires is_registered<T>
    auto send(T& message, uint32_t dest_mailbox) -> MailboxResult<void> {
        if (!running_) {
            return MailboxError::NotRunning;
        }
        
        if (dest_mailbox == 0) {
            return MailboxError::InvalidDestination;
        }
        
        // Serialize the message (automatically sets msg_type)
        [[maybe_unused]] auto result = Registry::serialize(message);
        
        // Send via TiMS
        auto tims_result = tims_.send(message, dest_mailbox);
        
        if (tims_result != TimsResult::SUCCESS) {
            return MailboxError::NetworkError;
        }
        
        return MailboxResult<void>();
    }
    
    // ========================================================================
    // Receive Operations
    // ========================================================================
    
    /**
     * @brief Receive a message of specific type (blocking)
     * 
     * @tparam T Message type to receive
     * @return Received message or error
     */
    template<typename T>
        requires is_registered<T>
    auto receive() -> MailboxResult<ReceivedMessage<T>> {
        if (!running_) {
            return MailboxError::NotRunning;
        }
        
        // Receive raw bytes from TiMS
        // Use SeRTial's buffer_type (std::byte, proper size)
        typename sertial::Message<T>::buffer_type buffer;
        auto bytes = tims_.receive_raw_bytes(buffer, std::chrono::seconds(1));
        
        if (bytes <= 0) {
            return MailboxError::NetworkError;
        }
        
        // Deserialize using span
        auto result = Registry::template deserialize<T>(
            std::span<const std::byte>(buffer.data(), bytes)
        );
        if (!result) {
            return MailboxError::SerializationError;
        }
        
        // Wrap in ReceivedMessage with metadata
        ReceivedMessage<T> received{
            .message = std::move(*result),
            .timestamp = result->header.timestamp,
            .sequence_number = result->header.seq_number
        };
        
        return received;
    }
    
    /**
     * @brief Try to receive a message without blocking
     * 
     * @tparam T Message type to receive
     * @return Message if available, empty optional otherwise
     */
    template<typename T>
        requires is_registered<T>
    auto try_receive() -> std::optional<ReceivedMessage<T>> {
        // Use -1 to indicate non-blocking mode (TIMS_NONBLOCK)
        // This gets converted to -1ns which TiMS recognizes
        auto result = receive_for<T>(std::chrono::milliseconds(-1));
        if (result) {
            return *result;
        }
        return std::nullopt;
    }
    
    /**
     * @brief Receive a message with timeout
     * 
     * @tparam T Message type to receive
     * @param timeout Maximum time to wait
     * @return Received message or error (including timeout)
     */
    template<typename T>
        requires is_registered<T>
    auto receive_for(std::chrono::milliseconds timeout) -> MailboxResult<ReceivedMessage<T>> {
        if (!running_) {
            return MailboxError::NotRunning;
        }
        
        // Receive with timeout from TiMS
        // Use SeRTial's buffer_type (std::byte, proper size)
        typename sertial::Message<T>::buffer_type buffer;
        auto bytes = tims_.receive_raw_bytes(buffer, timeout);
        
        if (bytes == 0) {
            return MailboxError::Timeout;
        }
        
        if (bytes < 0) {
            return MailboxError::NetworkError;
        }
        
        // Deserialize using span
        auto result = Registry::template deserialize<T>(
            std::span<const std::byte>(buffer.data(), bytes)
        );
        if (!result) {
            return MailboxError::SerializationError;
        }
        
        // Wrap in ReceivedMessage with metadata
        ReceivedMessage<T> received{
            .message = std::move(*result),
            .timestamp = result->header.timestamp,
            .sequence_number = result->header.seq_number
        };
        
        return received;
    }
    
    /**
     * @brief Receive any registered message type using a visitor
     * 
     * The visitor will be called with the received message, allowing
     * runtime dispatch based on the actual message type received.
     * 
     * @param visitor Callable that accepts any registered message type
     * @return Success or error
     * 
     * Example:
     * @code
     * mailbox.receive_any([](auto&& msg) {
     *     using T = std::decay_t<decltype(msg)>;
     *     if constexpr (std::is_same_v<T, ReceivedMessage<StatusMessage>>) {
     *         std::cout << "Status: " << msg->payload.status_code << "\n";
     *     }
     * });
     * @endcode
     */
    template<typename Visitor>
    auto receive_any(Visitor&& visitor) -> MailboxResult<void> {
        if (!running_) {
            return MailboxError::NotRunning;
        }
        
        // Receive raw bytes
        // Use largest message size from registry since we don't know type in advance
        constexpr size_t buffer_size = Registry::max_message_size;
        std::array<std::byte, buffer_size> buffer;
        auto bytes = tims_.receive_raw_bytes(buffer, std::chrono::seconds(1));
        
        if (bytes <= 0) {
            return MailboxError::NetworkError;
        }
        
        // Parse header to get message type
        if (static_cast<size_t>(bytes) < sizeof(TimsHeader)) {
            return MailboxError::InvalidMessage;
        }
        
        TimsHeader header;
        std::memcpy(&header, buffer.data(), sizeof(TimsHeader));
        MessageType msg_type = static_cast<MessageType>(header.msg_type);
        
        // Use registry to dispatch based on runtime type
        bool success = Registry::visit(msg_type, 
            std::span<const std::byte>(buffer.data(), bytes),
            [&visitor, &header](auto&& msg) {
                using MsgType = std::decay_t<decltype(msg)>;
                ReceivedMessage<MsgType> received{
                    .message = std::move(msg),
                    .timestamp = header.timestamp,
                    .sequence_number = header.seq_number
                };
                std::forward<Visitor>(visitor)(received);
            });
        
        if (!success) {
            return MailboxError::InvalidMessage;
        }
        
        return MailboxResult<void>();
    }
    
    // ========================================================================
    // Utility Operations
    // ========================================================================
    
    /**
     * @brief Clear all pending messages from the mailbox
     * @return Success or error
     */
    auto clean() -> MailboxResult<void> {
        if (!running_) {
            return MailboxError::NotRunning;
        }
        
        // TiMS doesn't expose clean directly, so we drain messages
        // Use largest message size from registry to handle any message type
        constexpr size_t buffer_size = Registry::max_message_size;
        std::array<std::byte, buffer_size> buffer;
        while (tims_.receive_raw_bytes(buffer, std::chrono::milliseconds(10)) > 0) {
            // Discard messages
        }
        
        return MailboxResult<void>();
    }
    
    /**
     * @brief Get the mailbox ID
     */
    uint32_t mailbox_id() const {
        return config_.mailbox_id;
    }
    
    /**
     * @brief Get number of messages sent
     */
    uint64_t messages_sent() const {
        return tims_.get_messages_sent();
    }
    
    /**
     * @brief Get number of messages received
     */
    uint64_t messages_received() const {
        return tims_.get_messages_received();
    }
    
private:
    // Convert MailboxConfig to TimsConfig
    static TimsConfig create_tims_config(const MailboxConfig& config) {
        TimsConfig tims_config;
        tims_config.mailbox_id = config.mailbox_id;
        tims_config.mailbox_name = config.mailbox_name.empty() 
            ? ("mailbox_" + std::to_string(config.mailbox_id))
            : config.mailbox_name;
        tims_config.max_msg_size = config.max_message_size;
        tims_config.priority = config.send_priority;
        tims_config.realtime = config.realtime;
        return tims_config;
    }
    
    MailboxConfig config_;
    TimsWrapper tims_;
    std::atomic<bool> running_;
};

// ============================================================================
// Convenience Type Aliases
// ============================================================================

/**
 * @brief Standard mailbox with common message types
 */
using StandardMailbox = Mailbox<
    CommandMessage,
    StatusMessage,
    ErrorMessage,
    AckMessage,
    SensorMessage,
    RobotStateMessage
>;

/**
 * @brief Create a custom mailbox with standard types plus user types
 */
template<typename... CustomTypes>
using CustomMailbox = Mailbox<
    CommandMessage,
    StatusMessage,
    ErrorMessage,
    AckMessage,
    CustomTypes...
>;

} // namespace commrat
