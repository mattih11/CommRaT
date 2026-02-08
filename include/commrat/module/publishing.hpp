/**
 * @file publishing.hpp
 * @brief Publishing logic for single-output and multi-output modules
 * 
 * Handles publishing data to subscribers with type-specific filtering
 * for multi-output modules. Supports both payload-only and TimsMessage-based
 * publishing with explicit timestamp control (Phase 6.10).
 */

#pragma once

#include <commrat/messaging/message_registry.hpp>
#include <commrat/mailbox/registry_mailbox.hpp>
#include <commrat/messages.hpp>  // TimsMessage definition
#include <commrat/module/helpers/address_helpers.hpp>  // extract_message_type_from_address
#include <iostream>
#include <tuple>
#include <mutex>
#include <vector>

namespace commrat {

/**
 * @brief Publishing logic for producer modules
 * 
 * Handles publishing to subscribers with type-specific filtering.
 * Single-output modules publish to all subscribers.
 * Multi-output modules filter based on subscriber's expected message type.
 * 
 * Template parameters:
 * - UserRegistry: User message registry
 * - OutputData: Single output type (or void for multi-output)
 * - SubscriberManager: Provides subscribers list and mutex
 */
template<
    typename UserRegistry,
    typename OutputData,
    typename SubscriberManager
>
class Publisher {
protected:
    using CmdMailbox = RegistryMailbox<UserRegistry>;
    
    // References to module resources (set by derived class)
    SubscriberManager* subscriber_manager_{nullptr};
    CmdMailbox* cmd_mailbox_{nullptr};
    std::string module_name_;
    
public:
    void set_subscriber_manager(SubscriberManager* mgr) { subscriber_manager_ = mgr; }
    void set_cmd_mailbox(CmdMailbox* mbx) { cmd_mailbox_ = mbx; }
    void set_module_name(const std::string& name) { module_name_ = name; }
    
    /**
     * @brief Mailbox type offsets (must match Module's enum)
     */
    enum class MailboxType : uint8_t {
        CMD = 0,
        WORK = 16,
        DATA = 32
    };
    
    /**
     * @brief Create TimsMessage with explicit timestamp
     * Phase 6.10: Single source of truth for timestamps (header.timestamp)
     */
    template<typename T>
    static TimsMessage<T> create_tims_message(T&& payload, uint64_t timestamp_ns) {
        TimsMessage<T> msg{
            .header = {
                .msg_type = 0,     // serialize() will set this
                .msg_size = 0,     // serialize() will set this
                .timestamp = timestamp_ns,  // ONE SOURCE OF TRUTH
                .seq_number = 0,   // TiMS will set this
                .flags = 0
            },
            .payload = std::forward<T>(payload)
        };
        return msg;
    }
    
    /**
     * @brief Single-output publishing (only enabled when OutputData is not void)
     * Publishes data to all subscribers' DATA mailboxes
     */
    template<typename T = OutputData>
        requires (!std::is_void_v<T>)
    void publish_to_subscribers(T& data) {
        auto& mutex = subscriber_manager_->get_subscribers_mutex();
        auto& subscribers = subscriber_manager_->get_subscribers();
        
        std::lock_guard<std::mutex> lock(mutex);
        for (uint32_t subscriber_base_addr : subscribers) {
            // Send to subscriber's DATA mailbox (base + 32)
            uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
            auto result = cmd_mailbox_->send(data, subscriber_data_mbx);
            if (!result) {
                std::cout << "[" << module_name_ << "] Send failed to subscriber " << subscriber_base_addr << "\n";
            }
        }
    }
    
    /**
     * @brief Publish TimsMessage<T> for single output
     * Phase 6.10: Uses explicit timestamp from header
     */
    template<typename T>
    void publish_tims_message(TimsMessage<T>& tims_msg) {
        auto& mutex = subscriber_manager_->get_subscribers_mutex();
        auto& subscribers = subscriber_manager_->get_subscribers();
        
        std::lock_guard<std::mutex> lock(mutex);
        for (uint32_t subscriber_base_addr : subscribers) {
            uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
            // Phase 6.10: Send with explicit timestamp from header
            auto result = cmd_mailbox_->send(tims_msg.payload, subscriber_data_mbx, tims_msg.header.timestamp);
            if (!result) {
                std::cout << "[" << module_name_ << "] Send failed to subscriber " << subscriber_base_addr << "\n";
            }
        }
    }
    
    /**
     * @brief Send output only if type matches subscriber's expected type
     * Multi-output filtering: Each subscriber receives only their subscribed type
     */
    template<typename OutputType>
    void send_if_type_matches(uint16_t subscriber_type_id_low, OutputType& output, uint32_t dest_mailbox) {
        // Get the full message ID for this output type
        constexpr uint32_t output_msg_id = UserRegistry::template get_message_id<OutputType>();
        constexpr uint16_t output_type_id_low = static_cast<uint16_t>(output_msg_id & 0xFFFF);
        
        // Only send if types match
        if (output_type_id_low == subscriber_type_id_low) {
            auto result = cmd_mailbox_->send(output, dest_mailbox);
            if (!result) {
                std::cout << "[" << module_name_ << "] Send failed to subscriber " 
                          << std::hex << dest_mailbox << std::dec << "\n";
            }
        }
    }
    
    /**
     * @brief Multi-output publishing implementation
     * Uses index sequence to iterate over tuple elements at compile time
     */
    template<typename... Ts, std::size_t... Is>
    void publish_multi_outputs_impl(std::tuple<Ts...>& outputs, std::index_sequence<Is...>) {
        auto& mutex = subscriber_manager_->get_subscribers_mutex();
        auto& subscribers = subscriber_manager_->get_subscribers();
        
        std::lock_guard<std::mutex> lock(mutex);
        for (uint32_t subscriber_base_addr : subscribers) {
            uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
            
            // Extract the message type ID that this subscriber expects
            uint16_t subscriber_type_id_low = extract_message_type_from_address(subscriber_base_addr);
            
            // Send each output only if it matches the subscriber's type
            (void)std::initializer_list<int>{
                (send_if_type_matches<std::tuple_element_t<Is, std::tuple<Ts...>>>(
                    subscriber_type_id_low,
                    std::get<Is>(outputs),
                    subscriber_data_mbx
                ), 0)...
            };
        }
    }
    
    /**
     * @brief Publish multiple outputs (tuple) to subscribers
     * Each subscriber receives only outputs matching their subscribed type
     */
    template<typename... Ts>
    void publish_multi_outputs(std::tuple<Ts...>& outputs) {
        publish_multi_outputs_impl(outputs, std::index_sequence_for<Ts...>{});
    }
    
    /**
     * @brief Publish multi-outputs with explicit timestamp
     * Phase 6.10: Wraps each output in TimsMessage with timestamp
     * 
     * Note: Currently delegates to publish_multi_outputs (timestamp not yet used)
     * Future enhancement: Wrap each output in TimsMessage with timestamp
     */
    template<typename... Ts>
    void publish_multi_outputs_with_timestamp(std::tuple<Ts...>& outputs, uint64_t timestamp_ns) {
        // For multi-output, we still use the existing publish_multi_outputs
        // because each output goes through send_if_type_matches which wraps in TimsMessage
        // The timestamp is not used here yet - future enhancement could wrap each output
        publish_multi_outputs(outputs);
    }
};

} // namespace commrat
