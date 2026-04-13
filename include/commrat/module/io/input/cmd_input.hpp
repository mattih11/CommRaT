#pragma once

#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/helpers/type_name.hpp"
#include "commrat/mailbox/mailbox.hpp"
#include "commrat/messaging/data_with_commands.hpp"
#include "commrat/platform/timestamp.hpp"
#include <cstdint>

namespace commrat {

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
class CmdInput {
public:
    // Extract data message (unwrap DataWithCommands if needed)
    using DataMessage = ExtractDataMessage_t<OutputType>;
    
    // Extract command list (empty tuple if no commands)
    using CommandList = ExtractCommands_t<OutputType>;
    
    // Compile-time type_id calculation from output payload type
    static constexpr uint32_t output_message_id = Registry::template get_message_id<OutputType>();
    static constexpr uint8_t type_id = static_cast<uint8_t>(output_message_id & 0xFF);
    
    /**
     * @brief Default constructor (for tuple initialization)
     * 
     * Creates uninitialized input. Must call initialize() or use parametrized constructor.
     */
    CmdInput()
        : work_mbx_(nullptr)
        , producer_system_id_(0)
        , producer_instance_id_(0)
        , producer_cmd_address_(0)
        , cmd_timeout_(Milliseconds(100))
    {}
    
    /**
     * @brief Initialize input with mailbox and producer address
     * 
     * Call this after default construction to set up the input.
     */
    void initialize(typename Registry::System::WorkMailbox& work_mbx,
                    uint8_t producer_system_id,
                    uint8_t producer_instance_id,
                    Duration cmd_timeout = Milliseconds(100)) {
        work_mbx_ = &work_mbx;
        producer_system_id_ = producer_system_id;
        producer_instance_id_ = producer_instance_id;
        producer_cmd_address_ = encode_address(type_id, producer_system_id, 
                                               producer_instance_id, 0);  // CMD mailbox index = 0
        cmd_timeout_ = cmd_timeout;
    }
    
    /**
     * @brief Construct command input
     * @param work_mbx Shared mailbox for receiving replies
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     * @param cmd_timeout Default timeout for commands
     */
    CmdInput(MailboxFor<Registry>& work_mbx,
             uint8_t producer_system_id,
             uint8_t producer_instance_id,
             Duration cmd_timeout = Milliseconds(100))
        : work_mbx_(&work_mbx)
        , producer_system_id_(producer_system_id)
        , producer_instance_id_(producer_instance_id)
        , producer_cmd_address_(encode_address(type_id, producer_system_id, 
                                               producer_instance_id, 0))  // CMD mailbox index = 0
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
    
protected:
    typename Registry::System::WorkMailbox* work_mbx_;  ///< Shared work mailbox for RPC (pointer for default construction, non-owning)
    uint8_t producer_system_id_;                        ///< Producer's system ID
    uint8_t producer_instance_id_;                      ///< Producer's instance ID
    uint32_t producer_cmd_address_;                     ///< Producer's CMD mailbox address
    Duration cmd_timeout_;                          ///< Default timeout for commands
};

} // namespace commrat
