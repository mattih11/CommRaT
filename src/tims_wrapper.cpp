#include "commrat/tims_wrapper.hpp"
#include <cstring>
#include <chrono>

namespace commrat {

TimsWrapper::TimsWrapper(const TimsConfig& config)
    : config_(config)
    , tims_fd_(-1)
    , is_initialized_(false)
    , messages_sent_(0)
    , messages_received_(0)
    , sequence_number_(0) {
}

TimsWrapper::~TimsWrapper() {
    shutdown();
}

TimsWrapper::TimsWrapper(TimsWrapper&& other) noexcept
    : config_(std::move(other.config_))
    , tims_fd_(other.tims_fd_)
    , is_initialized_(other.is_initialized_.load())
    , messages_sent_(other.messages_sent_.load())
    , messages_received_(other.messages_received_.load())
    , sequence_number_(other.sequence_number_) {
    other.tims_fd_ = -1;
    other.is_initialized_ = false;
}

TimsWrapper& TimsWrapper::operator=(TimsWrapper&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        config_ = std::move(other.config_);
        tims_fd_ = other.tims_fd_;
        is_initialized_ = other.is_initialized_.load();
        messages_sent_ = other.messages_sent_.load();
        messages_received_ = other.messages_received_.load();
        sequence_number_ = other.sequence_number_;
        
        other.tims_fd_ = -1;
        other.is_initialized_ = false;
    }
    return *this;
}

TimsResult TimsWrapper::initialize() {
    if (is_initialized_) {
        return TimsResult::SUCCESS;
    }
    
    // Create TIMS mailbox (this handles socket creation, connection to router, and mailbox init)
    tims_fd_ = tims_mbx_create(config_.mailbox_id, 
                               10,  // message slots (adjust as needed)
                               config_.max_msg_size,
                               nullptr,  // let TIMS allocate buffer
                               0);  // buffer size (0 = auto)
    if (tims_fd_ < 0) {
        return TimsResult::ERROR_INIT;
    }
    
    is_initialized_ = true;
    return TimsResult::SUCCESS;
}

void TimsWrapper::shutdown() {
    if (!is_initialized_) {
        return;
    }
    
    if (tims_fd_ >= 0) {
        // Remove mailbox (this also closes the socket)
        tims_mbx_remove(tims_fd_);
        tims_fd_ = -1;
    }
    
    is_initialized_ = false;
}

TimsResult TimsWrapper::send_raw(const void* data, size_t size, uint32_t dest_mailbox_id) {
    if (!is_initialized_ || tims_fd_ < 0) {
        return TimsResult::ERROR_NOT_INITIALIZED;
    }
    
    if (!data || size == 0 || size > config_.max_msg_size) {
        return TimsResult::ERROR_INVALID_MESSAGE;
    }
    
    // Prepare TIMS message header
    tims_msg_head head;
    tims_fill_head(&head, 
                   0,  // type (user-defined, use 0 for data)
                   dest_mailbox_id,  // dest
                   config_.mailbox_id,  // src
                   config_.priority,  // priority
                   sequence_number_++,  // seq_nr
                   0,  // flags (will be set by tims_fill_head)
                   static_cast<uint32_t>(TIMS_HEADLEN + size));  // msglen
    
    // Send message using iovec for header + data
    struct iovec vec[1];
    vec[0].iov_base = const_cast<void*>(data);
    vec[0].iov_len = size;
    
    ssize_t result = tims_sendmsg(tims_fd_, &head, vec, 1, 0);
    
    if (result < 0) {
        return TimsResult::ERROR_SEND;
    }
    
    messages_sent_.fetch_add(1, std::memory_order_relaxed);
    
    return TimsResult::SUCCESS;
}

ssize_t TimsWrapper::receive_raw(void* buffer, size_t buffer_size, 
                                  std::chrono::milliseconds timeout) {
    if (!is_initialized_ || tims_fd_ < 0) {
        return -1;
    }
    
    // Convert timeout to nanoseconds for TIMS
    int64_t timeout_ns = timeout.count() * 1000000;  // ms to ns
    
    tims_msg_head head;
    ssize_t bytes_received = tims_recvmsg_timed(
        tims_fd_,
        &head,
        buffer,
        buffer_size,
        timeout_ns,
        0  // flags
    );
    
    if (bytes_received < 0) {
        return bytes_received;  // Error
    }
    
    // Parse header for byte order
    tims_parse_head_byteorder(&head);
    
    messages_received_.fetch_add(1, std::memory_order_relaxed);
    
    return bytes_received;
}

bool TimsWrapper::has_message() const {
    if (!is_initialized_ || tims_fd_ < 0) {
        return false;
    }
    
    // Use tims_peek_timed with zero timeout (non-blocking)
    tims_msg_head* p_head = nullptr;
    int ret = tims_peek_timed(tims_fd_, &p_head, TIMS_NONBLOCK);
    
    if (ret == 0 && p_head != nullptr) {
        // Message available, unlock it
        tims_peek_end(tims_fd_);
        return true;
    }
    
    return false;
}

} // namespace commrat
