#pragma once

#include "../messages.hpp"
#include "timestamp.hpp"
#include <memory>
#include <string>
#include <functional>
#include <optional>
#include <atomic>
#include <span>
#include <array>

// TIMS API includes
#include <main/tims/tims.h>
#include <main/tims/tims_api.h>

namespace commrat {

// TiMS configuration
struct TimsConfig {
    std::string mailbox_name;
    uint32_t mailbox_id;
    size_t max_msg_size;
    uint32_t priority;
    bool realtime;
    
    TimsConfig() 
        : mailbox_name("default")
        , mailbox_id(0)
        , max_msg_size(4096)
        , priority(0)
        , realtime(false) {}
};

// Result type for operations
enum class TimsResult {
    SUCCESS = 0,
    ERROR_INIT = -1,
    ERROR_SEND = -2,
    ERROR_RECEIVE = -3,
    ERROR_TIMEOUT = -4,
    ERROR_INVALID_MESSAGE = -5,
    ERROR_NOT_INITIALIZED = -6
};

// Modern C++ wrapper around TiMS with compile-time safety
class TimsWrapper {
public:
    explicit TimsWrapper(const TimsConfig& config);
    ~TimsWrapper();
    
    // Delete copy, allow move
    TimsWrapper(const TimsWrapper&) = delete;
    TimsWrapper& operator=(const TimsWrapper&) = delete;
    TimsWrapper(TimsWrapper&&) noexcept;
    TimsWrapper& operator=(TimsWrapper&&) noexcept;
    
    // Initialize the mailbox
    TimsResult initialize();
    
    // Shutdown the mailbox
    void shutdown();
    
    // Send a message with compile-time type safety and SeRTial serialization
    template<typename T>
    TimsResult send(T& message, uint32_t dest_mailbox_id) {
        static_assert(is_commrat_message_v<T>, "T must be a CommRaT message type");
        
        if (!is_initialized_) {
            return TimsResult::ERROR_NOT_INITIALIZED;
        }
        
        // Serialize the message using SeRTial (returns Result with std::byte buffer)
        auto result = serialize_message(message);
        
        // Convert std::byte span to void* for TIMS
        auto view = result.view();
        return send_raw(view.data(), view.size(), dest_mailbox_id);
    }
    
    // Receive a message with compile-time type safety and SeRTial deserialization
    template<typename T>
    std::optional<T> receive(Milliseconds timeout = Milliseconds(0)) {
        static_assert(is_commrat_message_v<T>, "T must be a CommRaT message type");
        
        if (!is_initialized_) {
            return std::nullopt;
        }
        
        // Use compile-time sized buffer from SeRTial
        constexpr size_t buffer_size = sertial::Message<T>::max_buffer_size;
        std::array<uint8_t, buffer_size> buffer;
        
        ssize_t received_size = receive_raw(buffer.data(), buffer.size(), timeout);
        
        if (received_size > 0) {
            auto result = deserialize_message<T>(buffer.data(), static_cast<size_t>(received_size));
            if (result) {
                return *result;  // DeserializeResult has operator*
            }
        }
        
        return std::nullopt;
    }
    
    // Check if there's a message waiting
    bool has_message() const;
    
    // Get mailbox info
    uint32_t get_mailbox_id() const { return config_.mailbox_id; }
    const std::string& get_mailbox_name() const { return config_.mailbox_name; }
    bool is_initialized() const { return is_initialized_; }
    
    // Statistics
    uint64_t get_messages_sent() const { return messages_sent_.load(); }
    uint64_t get_messages_received() const { return messages_received_.load(); }
    
    // Modern C++ interface using std::span<std::byte>
    // Casting to void* happens here at the TiMS boundary
    ssize_t receive_raw_bytes(std::span<std::byte> buffer, Milliseconds timeout) {
        return receive_raw(buffer.data(), buffer.size(), timeout);
    }
    
private:
    TimsResult send_raw(const void* data, size_t size, uint32_t dest_mailbox_id);
    ssize_t receive_raw(void* buffer, size_t buffer_size, Milliseconds timeout);
    
    TimsConfig config_;
    int tims_fd_; // TIMS file descriptor
    std::atomic<bool> is_initialized_;
    std::atomic<uint64_t> messages_sent_;
    std::atomic<uint64_t> messages_received_;
    uint32_t sequence_number_;
};

} // namespace commrat
