#pragma once

#include <corerat/platform/threading.hpp>
#include <corerat/platform/timestamp.hpp>

#include <cstdint>

namespace commrat {

/**
 * @brief Serializes synchronous request/reply transactions on a WORK mailbox.
 *
 * A mailbox receive removes the next message even when it is not the reply a
 * caller expected. Keeping the lock for the complete transaction guarantees
 * that only one module thread can consume WORK replies at a time.
 */
template<typename Registry>
class RpcClient {
public:
    using WorkMailbox = typename Registry::System::WorkMailbox;

    explicit RpcClient(WorkMailbox& mailbox)
        : mailbox_(&mailbox) {}

    [[nodiscard]] uint32_t mailbox_id() const {
        return mailbox_->mailbox_id();
    }

    template<typename RequestPayload, typename ReplyPayload>
    bool transact(corerat::WireMessage<RequestPayload>& request,
                  corerat::WireMessage<ReplyPayload>& reply,
                  uint32_t target_address,
                  corerat::Duration timeout) {
        corerat::Lock lock(mutex_);

        request.header.dest = target_address;
        request.header.src = mailbox_->mailbox_id();

        if (!mailbox_->send(request, target_address)) {
            return false;
        }

        const auto deadline = corerat::Time::now() + corerat::Time::to_nanoseconds(timeout);
        while (corerat::Time::now() < deadline) {
            const auto remaining_ns = static_cast<int64_t>(deadline - corerat::Time::now());
            if (!mailbox_->receive(
                    reply,
                    corerat::Duration::nanoseconds(remaining_ns > 0 ? remaining_ns : 0))) {
                continue;
            }

            if (reply.header.src == target_address &&
                reply.header.msg_type == Registry::template get_message_id<ReplyPayload>()) {
                return true;
            }
        }

        return false;
    }

private:
    WorkMailbox* mailbox_;
    corerat::Mutex mutex_;
};

} // namespace commrat