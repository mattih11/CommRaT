#pragma once

/**
 * @file command_handler.hpp
 * @brief Command handling service for Module2
 * 
 * Provides:
 * - Command dispatch for CMD mailbox per output
 * - System command handling (Subscribe, GetData)
 * - User command delegation to on_command<> overrides
 * - Clean visitor pattern using registry data
 */

#include "commrat/module/helpers/command_extraction.hpp"
#include "commrat/messaging/registry_utils.hpp"
#include "commrat/messaging/system/data_request_messages.hpp"
#include <iostream>

namespace commrat {

/**
 * @brief Command Handler Service - Dispatches commands for outputs
 * 
 * Handles commands received on per-output CMD mailboxes:
 * - System commands: Subscribe, Unsubscribe, GetData (via ModuleOutput)
 * - User commands: Custom commands (via on_command<> override)
 * 
 * Clean visitor pattern using single source of truth from registry.
 * 
 * @tparam Registry The message registry type
 * @tparam IOBuilder The I/O builder type
 * @tparam IOTuple The I/O tuple type
 */
template<typename Registry, typename IOBuilder, typename IOTuple>
class CommandHandler {
protected:
    // ========================================================================
    // Command Type Extraction (Single Source of Truth)
    // ========================================================================
    
    /**
     * @brief Detect if type is GetDataRequestPayload<T>
     */
    template<typename T>
    struct is_get_data_request_payload : std::false_type {};
    
    template<typename T>
    struct is_get_data_request_payload<GetDataRequestPayload<T>> : std::true_type {
        using DataType = T;
    };
    
    /**
     * @brief Detect if type is GetNextDataRequestPayload<T>
     */
    template<typename T>
    struct is_get_next_data_request_payload : std::false_type {};
    
    template<typename T>
    struct is_get_next_data_request_payload<GetNextDataRequestPayload<T>> : std::true_type {
        using DataType = T;
    };
    
    /**
     * @brief Get system commands (always included)
     * 
     * Returns tuple<SubscribeRequestPayload, UnsubscribeRequestPayload>
     * System messages from subscription protocol.
     * Note: GetDataRequestPayload<T> and GetNextDataRequestPayload<T> are templates
     * and cannot be listed here; they are detected via is_get_data_request_payload<T>.
     */
    using SystemCommands = std::tuple<SubscribeRequestPayload, UnsubscribeRequestPayload>;

    /**
     * @brief Detect if a type is any system command (fixed or template-based)
     */
    template<typename T>
    static constexpr bool is_system_command_v =
        is_in_tuple_v<T, SystemCommands> ||
        is_get_data_request_payload<T>::value ||
        is_get_next_data_request_payload<T>::value;
    
    /**
     * @brief Get tuple index for logical output index (compile-time helper)
     */
    template<size_t OutputIndex>
    static constexpr size_t get_output_tuple_index() {
        constexpr auto indices = IOBuilder::output_indices();
        return indices[OutputIndex];
    }
    
    /**
     * @brief Get user commands for specific output
     */
    template<size_t OutputIndex>
    using UserCommandsFor = ExtractUserCommands_t<
        typename std::tuple_element_t<
            get_output_tuple_index<OutputIndex>(),
            IOTuple
        >::message_def_type
    >;
    
    // ========================================================================
    // Command Visitor Pattern
    // ========================================================================
    
    /**
     * @brief Visit system commands - handles Subscribe, GetData, etc.
     * 
     * @return true if message was a system command (handled), false otherwise
     */
    template<size_t OutputIndex, typename Output, typename ReceivedMsg>
    bool visit_system_command(Output& output, ReceivedMsg&& received_msg) {
        using CmdType = typename std::decay_t<decltype(received_msg)>::payload_type;
        
        // Covers: SubscribeRequestPayload, UnsubscribeRequestPayload,
        //         GetDataRequestPayload<T>, GetNextDataRequestPayload<T>
        if constexpr (is_system_command_v<CmdType>) {
            handle_system_command<OutputIndex>(output, received_msg);
            return true;
        }
        
        return false; // Not a system command
    }
    
    /**
     * @brief Helper to check if a type has a Reply member type
     */
    template<typename T, typename = void>
    struct has_reply_type : std::false_type {};
    
    template<typename T>
    struct has_reply_type<T, std::void_t<typename T::Reply>> : std::true_type {};
    
    template<typename T>
    static constexpr bool has_reply_type_v = has_reply_type<T>::value;
    
    /**
     * @brief Visit user commands - dispatches to on_command<> override
     * 
     * @return true if message was a user command (handled), false otherwise
     */
    template<size_t OutputIndex, typename Output, typename ReceivedMsg, typename DerivedModule>
    bool visit_user_command(Output& output, ReceivedMsg&& received_msg, DerivedModule* derived) {
        using CmdType = typename std::decay_t<decltype(received_msg)>::payload_type;
        using UserCommands = UserCommandsFor<OutputIndex>;
        
        // Only process request messages (those with a Reply type), not reply messages themselves
        if constexpr (has_reply_type_v<CmdType>) {
            // Simple tuple membership check
            if constexpr (is_in_tuple_v<CmdType, UserCommands>) {
                // CRTP: derived module must implement on_command<OutputIndex>(cmd, reply)
                typename CmdType::Reply reply{};
                derived->template on_command<OutputIndex>(received_msg.payload, reply);
                
                // Send reply via CMD mailbox (same pattern as system commands)
                auto& cmd_mailbox = output.get_cmd_mailbox();
                cmd_mailbox.send_reply(received_msg, reply);
                return true;
            }
        }
        
        return false; // Not a user command or is a reply message
    }
    
    /**
     * @brief Handle unknown command
     */
    template<size_t OutputIndex>
    void handle_unknown_command() {
        std::cerr << "WARNING: Unknown command received for output " << OutputIndex << "\n";
        // TODO: Send UnknownCommandReply via CMD mailbox
    }
    
private:
    // ========================================================================
    // System Command Dispatch
    // ========================================================================
    
    /**
     * @brief Handle specific system command
     * 
     * Dispatches to ModuleOutput's handler methods and sends replies.
     * Uses send_reply() to automatically route to request.header.src.
     */
    template<size_t OutputIndex, typename Output, typename ReceivedMsg>
    void handle_system_command(Output& output, ReceivedMsg&& received_msg) {
        using CmdType = typename std::decay_t<decltype(received_msg)>::payload_type;
        
        auto& cmd_mailbox = output.get_cmd_mailbox();
        
        if constexpr (std::is_same_v<CmdType, SubscribeRequestPayload>) {
            // Handle subscription request
            auto reply_payload = output.handle_subscribe_request(received_msg.payload);
            
            // Send reply using RACK-style send_reply (dest = received_msg.header.src)
            cmd_mailbox.send_reply(received_msg, reply_payload);
        } 
        else if constexpr (std::is_same_v<CmdType, UnsubscribeRequestPayload>) {
            // Handle unsubscribe request
            auto reply_payload = output.handle_unsubscribe_request(received_msg.payload);
            
            // Send reply using RACK-style send_reply (dest = received_msg.header.src)
            cmd_mailbox.send_reply(received_msg, reply_payload);
        }
        else if constexpr (is_get_data_request_payload<CmdType>::value) {
            // GetDataRequestPayload<DataT> - timestamp-synchronized query.
            // Guard: only compile when request data type matches this output's type.
            using RequestDataType = typename is_get_data_request_payload<CmdType>::DataType;
            if constexpr (std::is_same_v<RequestDataType, typename std::decay_t<Output>::Type>) {
                auto reply_payload = output.handle_get_data_request(received_msg.payload);
                cmd_mailbox.send_reply(received_msg, reply_payload);
            }
        }
        else if constexpr (is_get_next_data_request_payload<CmdType>::value) {
            // GetNextDataRequestPayload<DataT> - latest data query.
            using RequestDataType = typename is_get_next_data_request_payload<CmdType>::DataType;
            if constexpr (std::is_same_v<RequestDataType, typename std::decay_t<Output>::Type>) {
                auto reply_payload = output.handle_get_next_data_request(received_msg.payload);
                cmd_mailbox.send_reply(received_msg, reply_payload);
            }
        }
    }
};

} // namespace commrat
