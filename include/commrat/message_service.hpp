#pragma once

#include "tims_wrapper.hpp"
#include "messages.hpp"
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

namespace commrat {

// Message callback type
template<typename T>
using MessageCallback = std::function<void(const T&)>;

// Type-erased callback wrapper
using GenericCallback = std::function<void(const void*, size_t)>;

// High-level message service with type-safe dispatch
class MessageService {
public:
    explicit MessageService(const TimsConfig& config);
    ~MessageService();
    
    // Initialize the service
    bool start();
    
    // Shutdown the service
    void stop();
    
    // Send a message with automatic timestamping and sequencing
    template<typename T>
    TimsResult send(T& message, uint32_t dest_mailbox_id) {
        static_assert(is_commrat_message_v<T>, "T must be a valid message type");
        
        // Set message type from template parameter
        message.header.msg_type = static_cast<uint32_t>(T::message_type);
        
        // Update header with timestamp and sequence
        message.header.timestamp = get_timestamp();
        message.header.seq_number = next_sequence_number();
        
        return tims_.send(message, dest_mailbox_id);
    }
    
    // Register a type-safe callback for a specific message type
    template<typename T>
    void register_handler(MessageCallback<T> callback) {
        static_assert(is_commrat_message_v<T>, "T must be a CommRaT message type");
        
        std::lock_guard<std::mutex> lock(handler_mutex_);
        
        // Get message type from template parameter at compile time
        uint32_t msg_type = static_cast<uint32_t>(T::message_type);
        
        handlers_[msg_type] = [callback](const void* data, size_t size) {
            auto result = deserialize_message<T>(static_cast<const uint8_t*>(data), size);
            if (result) {  // DeserializeResult has operator bool()
                callback(*result);  // operator* to get value
            }
        };
    }
    
    // Unregister a handler
    void unregister_handler(uint32_t msg_type);
    
    // Manually process received messages (for non-threaded mode)
    void process_messages(size_t max_messages = 1);
    
    // Get service statistics
    uint64_t get_messages_sent() const { return tims_.get_messages_sent(); }
    uint64_t get_messages_received() const { return tims_.get_messages_received(); }
    bool is_running() const { return running_; }
    
private:
    void receive_thread_func();
    uint64_t get_timestamp() const;
    uint32_t next_sequence_number();
    
    TimsWrapper tims_;
    std::thread receive_thread_;
    std::atomic<bool> running_;
    std::atomic<uint32_t> sequence_counter_;
    
    std::mutex handler_mutex_;
    std::unordered_map<uint32_t, GenericCallback> handlers_;
    
    // Internal message buffer
    static constexpr size_t MAX_MESSAGE_SIZE = 4096;
    std::array<uint8_t, MAX_MESSAGE_SIZE> receive_buffer_;
};

} // namespace commrat
