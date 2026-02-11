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
#include <commrat/module/module_config.hpp>  // MailboxType enum
#include <iostream>
#include <tuple>
#include <mutex>
#include <vector>

namespace commrat {

/**
 * @brief Publishing logic for producer modules
 * 
 * Handles publishing to subscribers with type-specific filtering.
 * After unification, ALL modules use MailboxSets and access subscribers
 * via module_ptr_->get_output_subscribers(index).
 * 
 * Template parameters:
 * - UserRegistry: User message registry
 * - OutputData: Single output type (or void for multi-output)
 * - PublishMailboxT: Publish mailbox type (defaults to TypedMailbox<UserRegistry>)
 * - ModuleType: Module type for mailbox/subscriber access (REQUIRED after unification)
 */
template<
    typename UserRegistry,
    typename OutputData,
    typename PublishMailboxT = TypedMailbox<UserRegistry>,
    typename ModuleType = void  // Module type for multi-output mailbox access
>
class Publisher {
protected:
    // References to module resources (set by derived class)
    // REMOVED: subscriber_manager_ - ALL modules now use MultiOutputManager (post-unification)
    // REMOVED: publish_mailbox_ - ALL modules now use MailboxSets (post-unification)
    ModuleType* module_ptr_{nullptr};  // Typed pointer to Module for mailbox/subscriber access
    std::string module_name_;
    
public:
    // REMOVED: set_subscriber_manager() - no longer used after unification
    // REMOVED: set_publish_mailbox() - no longer used after unification
    void set_module_ptr(ModuleType* ptr) { module_ptr_ = ptr; }
    void set_module_name(const std::string& name) { module_name_ = name; }
    
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
     * 
     * After unification, ALL modules use MailboxSets and MultiOutputManager,
     * so we access mailbox via get_publish_mailbox_public<0>() and subscribers
     * via get_output_subscribers(0).
     */
    template<typename T = OutputData>
        requires (!std::is_void_v<T>)
    void publish_to_subscribers(T& data) {
        if constexpr (!std::is_void_v<ModuleType>) {
            // Use MailboxSet approach (unified path for all modules)
            auto& publish_mbx = module_ptr_->template get_publish_mailbox_public<0>();
            // Get output-specific subscriber list (index 0 for single-output)
            auto subscribers = module_ptr_->get_output_subscribers(0);
            
            for (uint32_t subscriber_base_addr : subscribers) {
                // Send to subscriber's DATA mailbox (base + MailboxType::DATA = base + 48)
                uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
                auto result = publish_mbx.send(data, subscriber_data_mbx);
                if (!result) {
                    std::cerr << "[" << module_name_ << "] Send failed to subscriber base=" << subscriber_base_addr 
                              << " data_mbx=" << subscriber_data_mbx << " error=" << static_cast<int>(result.get_error()) << "\n";
                }
            }
        }
    }
    
    /**
     * @brief Publish TimsMessage<T> for single output
     * Phase 6.10: Uses explicit timestamp from header
     * 
     * After unification, ALL modules use MailboxSets and MultiOutputManager,
     * so we access mailbox via get_publish_mailbox_public<0>() and subscribers
     * via get_output_subscribers(0).
     */
    template<typename T>
    void publish_tims_message(TimsMessage<T>& tims_msg) {
        if constexpr (!std::is_void_v<ModuleType>) {
            // Use MailboxSet approach (unified path for all modules)
            auto& publish_mbx = module_ptr_->template get_publish_mailbox_public<0>();
            // Get output-specific subscriber list (index 0 for single-output)
            auto subscribers = module_ptr_->get_output_subscribers(0);
            
            for (uint32_t subscriber_base_addr : subscribers) {
                uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
                // Phase 6.10: Send with explicit timestamp from header
                auto result = publish_mbx.send(tims_msg.payload, subscriber_data_mbx, tims_msg.header.timestamp);
                if (!result) {
                    std::cerr << "[" << module_name_ << "] Send failed to subscriber base=" << subscriber_base_addr 
                              << " data_mbx=" << subscriber_data_mbx << " error=" << static_cast<int>(result.get_error()) << "\n";
                }
            }
        }
    }
    
    /**
     * @brief Send output at given index if type matches subscriber's expected type
     * Phase 7.4: Uses Module's get_publish_mailbox<Index>() for per-output mailboxes
     * 
     * After unification, ModuleType is always set (never void), so we always
     * use the MailboxSet approach via get_publish_mailbox_public<Index>().
     */
    template<std::size_t Index, typename OutputType>
    void send_output_at_index(uint16_t subscriber_type_id_low, OutputType& output, uint32_t dest_mailbox) {
        // Get the full message ID for this output type
        constexpr uint32_t output_msg_id = UserRegistry::template get_message_id<OutputType>();
        constexpr uint16_t output_type_id_low = static_cast<uint16_t>(output_msg_id & 0xFFFF);
        
        // Only send if types match
        if (output_type_id_low == subscriber_type_id_low) {
            // Get the correct publish mailbox for this output index (unified path)
            if constexpr (!std::is_void_v<ModuleType>) {
                auto& publish_mbx = module_ptr_->template get_publish_mailbox_public<Index>();
                auto result = publish_mbx.send(output, dest_mailbox);
                if (!result) {
                    std::cout << "[" << module_name_ << "] Send failed for output[" << Index << "]\n";
                }
            }
        }
    }
    
    /**
     * @brief Multi-output publishing implementation
     * Phase 7: Each output publishes to its own subscriber list
     */
    template<typename... Ts, std::size_t... Is>
    void publish_multi_outputs_impl(std::tuple<Ts...>& outputs, std::index_sequence<Is...>) {
        // Each output index publishes to its own subscriber list
        (publish_output_at_index<Is>(std::get<Is>(outputs)), ...);
    }
    
    /**
     * @brief Publish specific output to its subscribers (Phase 7)
     */
    template<std::size_t Index, typename OutputType>
    void publish_output_at_index(OutputType& output) {
        if constexpr (!std::is_void_v<ModuleType>) {
            // Get subscribers for this specific output
            auto subscribers = module_ptr_->get_output_subscribers(Index);
            
            // Send to each subscriber's DATA mailbox
            auto& publish_mbx = module_ptr_->template get_publish_mailbox_public<Index>();
            for (uint32_t subscriber_base_addr : subscribers) {
                uint32_t dest_mailbox = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
                auto result = publish_mbx.send(output, dest_mailbox);
                if (!result) {
                    std::cout << "[" << module_name_ << "] Send failed for output[" << Index 
                              << "] to subscriber " << subscriber_base_addr << "\n";
                }
            }
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
