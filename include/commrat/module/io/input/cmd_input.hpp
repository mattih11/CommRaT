#pragma once

#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/helpers/type_name.hpp"
#include "commrat/module/rpc_client.hpp"
#include <corerat/ipc/mailbox.hpp>
#include "commrat/messaging/data_with_commands.hpp"
#include "commrat/messaging/registry_utils.hpp"
#include "commrat/messaging/system/lifecycle_messages.hpp"
#include "commrat/module/helpers/command_extraction.hpp"
#include <corerat/platform/timestamp.hpp>
#include <corerat/platform/duration.hpp>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace commrat {

using corerat::Duration;
using corerat::Milliseconds;
using corerat::Time;
using corerat::Timestamp;
template<typename T>
using TimsMessage = corerat::WireMessage<T>;
using TimsHeader  = corerat::WireHeader;

/**
 * @brief Command-only input interface (RACK RackProxy pattern)
 * 
 * Provides RPC interface to a remote module's output without receiving data.
 * Commands are type-specific - extracted from OutputType's command list.
 * 
 * Features:
 * - Compile-time type_id calculation from OutputType
 * - Compile-time command extraction from DataWithCommands
 * - Send-and-receive RPC with configurable timeout
 * - Type-safe command/reply pairs
 * - Error handling (timeout, error, not available)
 * - No data subscription
 * 
 * @tparam Registry Message registry
 * @tparam OutputType Output message type (Data or DataWithCommands)
 * 
 * Example:
 * @code
 * using SensorData = DataWithCommands<SensorPayload, CalibrateCmd, SetRateCmd>;
 * 
 * CmdInput<MyRegistry, SensorData> input(work_mbx, sys_id, inst_id);
 * 
 * // Send command (type-safe - only commands from SensorData::Commands allowed)
 * TimsMessage<CalibrateCmdPayload> cmd{...};
 * TimsMessage<CalibrateReplyPayload> reply;
 * bool ok = input.send_command(cmd, reply);
 * @endcode
 * 
 * RACK Equivalent: RackProxy::proxySendRecvDataCmd()
 * Modern approach: Templates + compile-time command extraction
 */
template<typename Registry, typename OutputType>
class RemoteHandle {
public:
    using Type = OutputType;
    // Extract data message (unwrap DataWithCommands if needed)
    using DataMessage = ExtractDataMessage_t<OutputType>;
    
    // Extract command list (empty tuple if no commands)
    using CommandList = registry::get_commands_for_t<OutputType, Registry>;
    
    // Compile-time type_id calculation from output payload type
    static constexpr uint32_t output_message_id = []() {
        if constexpr (std::is_void_v<OutputType>) {
            return uint32_t{0};
        } else {
            return Registry::template get_message_id<OutputType>();
        }
    }();
    static constexpr uint8_t type_id = static_cast<uint8_t>(output_message_id & 0xFF);
    
    /**
     * @brief Default constructor (for tuple initialization)
     * 
     * Creates uninitialized input. Must call initialize() or use parametrized constructor.
     */
    RemoteHandle()
        : rpc_client_(nullptr)
        , work_mbx_(nullptr)
        , producer_system_id_(0)
        , producer_instance_id_(0)
        , producer_cmd_address_(0)
        , producer_lifecycle_address_(0)
        , cmd_timeout_(Milliseconds(100))
    {}
    
    /**
     * @brief Initialize input with mailbox and producer address
     * 
     * Call this after default construction to set up the input.
     */
    void initialize(RpcClient<Registry>& rpc_client,
                    uint8_t producer_system_id,
                    uint8_t producer_instance_id,
                    Duration cmd_timeout = Milliseconds(100),
                    uint32_t lifecycle_address = 0) {
        rpc_client_ = &rpc_client;
        work_mbx_ = nullptr;
        producer_system_id_ = producer_system_id;
        producer_instance_id_ = producer_instance_id;
        producer_cmd_address_ = encode_address(type_id, producer_system_id, 
                                               producer_instance_id, 0);  // CMD mailbox index = 0
        producer_lifecycle_address_ = lifecycle_address != 0
            ? lifecycle_address
            : get_lifecycle_address<OutputType, Registry>(
                  producer_system_id, producer_instance_id);
        cmd_timeout_ = cmd_timeout;
    }
    
    /**
     * @brief Construct command input
     * @param work_mbx Shared mailbox for receiving replies
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     * @param cmd_timeout Default timeout for commands
     */
    RemoteHandle(MailboxFor<Registry>& work_mbx,
             uint8_t producer_system_id,
             uint8_t producer_instance_id,
             Duration cmd_timeout = Milliseconds(100))
        : rpc_client_(nullptr)
        , work_mbx_(&work_mbx)
        , producer_system_id_(producer_system_id)
        , producer_instance_id_(producer_instance_id)
        , producer_cmd_address_(encode_address(type_id, producer_system_id, 
                                               producer_instance_id, 0))  // CMD mailbox index = 0
          , producer_lifecycle_address_(get_lifecycle_address<OutputType, Registry>(
              producer_system_id, producer_instance_id))
        , cmd_timeout_(cmd_timeout)
    {}
    
    /**
     * @brief Send command and receive reply (RPC with timeout)
     * 
     * RACK proxySendRecvDataCmd pattern:
     * 1. Send command message to producer's CMD mailbox
     * 2. Block waiting for reply with timeout
     * 3. Filter replies by source address (ignore other messages)
     * 4. Handle error/timeout/not_available responses
     * 5. Return reply data via out-parameter
     * 
     * @tparam CmdType Command message type (must be registered)
     * @param command Command to send
     * @param reply Output parameter - receives reply if successful
     * @param timeout Command timeout (default: constructor value)
     * @return True if reply received, false on timeout/error
     */
    template<typename CmdType, typename ReplyType>
    bool send_command(const TimsMessage<CmdType>& command, 
                     TimsMessage<ReplyType>& reply,
                     Duration timeout = Duration::zero()) {
        // Use constructor default if not specified
        if (timeout == Duration::zero()) {
            timeout = cmd_timeout_;
        }
        
        // Create mutable copy for send (serialize modifies header)
        TimsMessage<CmdType> cmd_copy = command;

        if (rpc_client_) {
            return rpc_client_->transact(
                cmd_copy, reply, producer_cmd_address_, timeout);
        }
        
        // 1. Send command to producer's CMD mailbox
        auto send_result = work_mbx_->send(cmd_copy, producer_cmd_address_);
        if (!send_result) {
            return false;
        }
        
        // 2. Wait for reply (RACK while-loop pattern - filter by source)
        Timestamp deadline = Time::now() + Time::to_nanoseconds(timeout);
        while (Time::now() < deadline) {
            TimsMessage<ReplyType> received;
            auto remaining_ns = static_cast<int64_t>(deadline - Time::now());
            Duration remaining = Duration::nanoseconds(remaining_ns > 0 ? remaining_ns : 0);
            bool got_msg = work_mbx_->receive(
                received,
                remaining
            );
            
            if (!got_msg) {
                continue;  // Timeout on this attempt, loop until deadline
            }
            
            // 3. Filter by source address (RACK pattern)
            if (extract_type_id(received.header.src) == type_id &&
                extract_system_id(received.header.src) == producer_system_id_ &&
                extract_instance_id(received.header.src) == producer_instance_id_) {
                
                // 4. Got reply from correct producer
                reply = std::move(received);
                return true;
            }
            // Else: Message from different source, keep waiting
        }
        
        // Timeout - no reply from producer
        // Timeout - no reply from producer
        return false;
    }

    /**
     * @brief Send a command payload and receive its typed reply.
     *
     * This is the convenience RPC form for user modules. It uses the same
     * bounded, source-filtered WORK-mailbox receive loop as the lower-level
     * TimsMessage overload.
     */
    template<typename CmdType>
    std::optional<TimsMessage<typename CmdType::Reply>> send_command(
        const CmdType& command,
        Duration timeout = Duration::zero()) {
        static_assert(is_in_tuple_v<CmdType, CommandList>,
                      "Command type is not associated with this target output type");
        static_assert(Registry::template is_registered<CmdType>,
                      "Command type is not registered in the message registry");
        static_assert(Registry::template is_registered<typename CmdType::Reply>,
                      "Command reply type is not registered in the message registry");

        TimsMessage<CmdType> request{
            .header = {
                .msg_type = Registry::template get_message_id<CmdType>(),
                .msg_size = 0,
                .timestamp = Time::now(),
                .seq_number = 0,
                .dest = producer_cmd_address_,
                .src = rpc_client_ ? rpc_client_->mailbox_id()
                                   : (work_mbx_ ? work_mbx_->mailbox_id() : 0),
                .flags = 0
            },
            .payload = command
        };

        TimsMessage<typename CmdType::Reply> reply;
        if (!send_command(request, reply, timeout)) {
            return std::nullopt;
        }

        return reply;
    }

    std::optional<TimsMessage<LifecycleOnReplyPayload>> on(
        Duration timeout = Duration::zero()) {
        return send_lifecycle<LifecycleOnPayload, LifecycleOnReplyPayload>(
            LifecycleOnPayload{}, timeout);
    }

    std::optional<TimsMessage<LifecycleOffReplyPayload>> off(
        Duration timeout = Duration::zero()) {
        return send_lifecycle<LifecycleOffPayload, LifecycleOffReplyPayload>(
            LifecycleOffPayload{}, timeout);
    }

    std::optional<TimsMessage<LifecycleStatusReplyPayload>> status(
        Duration timeout = Duration::zero()) {
        return send_lifecycle<GetLifecycleStatusPayload, LifecycleStatusReplyPayload>(
            GetLifecycleStatusPayload{}, timeout);
    }

    [[nodiscard]] uint8_t producer_system_id() const { return producer_system_id_; }
    [[nodiscard]] uint8_t producer_instance_id() const { return producer_instance_id_; }
    [[nodiscard]] uint32_t producer_cmd_address() const { return producer_cmd_address_; }
    [[nodiscard]] uint32_t producer_lifecycle_address() const {
        return producer_lifecycle_address_;
    }

private:
    template<typename RequestPayload, typename ReplyPayload>
    std::optional<TimsMessage<ReplyPayload>> send_lifecycle(
        const RequestPayload& payload,
        Duration timeout) {
        static_assert(Registry::template is_registered<RequestPayload>);
        static_assert(Registry::template is_registered<ReplyPayload>);

        if (!rpc_client_) {
            return std::nullopt;
        }
        if (timeout == Duration::zero()) {
            timeout = cmd_timeout_;
        }

        TimsMessage<RequestPayload> request{
            .header = {
                .msg_type = Registry::template get_message_id<RequestPayload>(),
                .msg_size = 0,
                .timestamp = Time::now(),
                .seq_number = 0,
                .dest = producer_lifecycle_address_,
                .src = rpc_client_->mailbox_id(),
                .flags = 0
            },
            .payload = payload
        };
        TimsMessage<ReplyPayload> reply;
        if (!rpc_client_->transact(
                request, reply, producer_lifecycle_address_, timeout)) {
            return std::nullopt;
        }
        return reply;
    }
    
protected:
    RpcClient<Registry>* rpc_client_;                         ///< Shared serialized RPC client
    typename Registry::System::WorkMailbox* work_mbx_;  ///< Shared work mailbox for RPC (pointer for default construction, non-owning)
    uint8_t producer_system_id_;                        ///< Producer's system ID
    uint8_t producer_instance_id_;                      ///< Producer's instance ID
    uint32_t producer_cmd_address_;                     ///< Producer's CMD mailbox address
    uint32_t producer_lifecycle_address_;               ///< Producer's lifecycle mailbox address
    Duration cmd_timeout_;                          ///< Default timeout for commands
};

template<typename Registry, typename OutputType>
using CmdInput = RemoteHandle<Registry, OutputType>;

} // namespace commrat
