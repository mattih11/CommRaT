#include "commrat/message_service.hpp"
#include <chrono>
#include <cstring>

namespace commrat {

MessageService::MessageService(const TimsConfig& config)
    : tims_(config)
    , running_(false)
    , sequence_counter_(0)
    , receive_buffer_{} {
}

MessageService::~MessageService() {
    stop();
}

bool MessageService::start() {
    if (running_) {
        return true;
    }
    
    auto result = tims_.initialize();
    if (result != TimsResult::SUCCESS) {
        return false;
    }
    
    running_ = true;
    
    // Start the receive thread
    receive_thread_ = std::thread(&MessageService::receive_thread_func, this);
    
    return true;
}

void MessageService::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    tims_.shutdown();
}

void MessageService::unregister_handler(uint32_t msg_type) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handlers_.erase(msg_type);
}

void MessageService::process_messages(size_t max_messages) {
    for (size_t i = 0; i < max_messages; ++i) {
        // Try to receive a message with short timeout
        auto timeout = std::chrono::milliseconds(100);
        
        // Receive raw message into buffer
        std::vector<uint8_t> buffer(4096);  // Max message size
        ssize_t bytes = tims_.receive_raw_bytes(buffer.data(), buffer.size(), timeout);
        
        if (bytes <= 0) {
            break;
        }
        
        buffer.resize(bytes);
        
        // Parse TIMS header to get message type
        if (buffer.size() < sizeof(TimsHeader)) {
            continue;
        }
        
        TimsHeader header;
        std::memcpy(&header, buffer.data(), sizeof(TimsHeader));
        
        // Find the handler for this message type
        std::lock_guard<std::mutex> lock(handler_mutex_);
        auto it = handlers_.find(header.msg_type);
        
        if (it != handlers_.end()) {
            // Call the handler with full message buffer
            it->second(buffer.data(), buffer.size());
        }
    }
}

void MessageService::receive_thread_func() {
    while (running_) {
        // Process up to 10 messages per iteration
        process_messages(10);
        
        // Small sleep to prevent busy-waiting
        // In a real RT system, you might use condition variables
        // or blocking receive with timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

uint64_t MessageService::get_timestamp() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

uint32_t MessageService::next_sequence_number() {
    return sequence_counter_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace commrat
