/**
 * @file subscription.hpp
 * @brief Subscription protocol implementation for producer-consumer patterns
 * 
 * Handles SubscribeRequest/Reply and UnsubscribeRequest/Reply protocol,
 * subscriber management, and subscription state tracking.
 */

#pragma once

#include <commrat/module/module_config.hpp>
#include <commrat/module/helpers/address_helpers.hpp>
#include <commrat/messaging/system/system_registry.hpp>
#include <iostream>
#include <vector>
#include <mutex>
#include <algorithm>
#include <optional>
#include <thread>
#include <chrono>

namespace commrat {

// Forward declarations for mailbox types
template<typename Registry>
class RegistryMailbox;

/**
 * @brief Subscription state tracker for multi-input modules
 */
struct SubscriptionState {
    bool subscribed{false};        // SubscribeRequest sent
    bool reply_received{false};    // SubscribeReply received
    uint32_t actual_period_ms{0};  // Period from reply
};

/**
 * @brief Subscription protocol handler
 * 
 * Template parameters:
 * - Registry: Message registry (for message ID lookups)
 * - has_continuous_input: Module has single continuous input
 * - has_multi_input: Module has multiple synchronized inputs
 * - InputData: Single input type (for has_continuous_input)
 * - InputTypesTuple: Tuple of input types (for has_multi_input)
 * - InputCount: Number of inputs (for has_multi_input)
 * - OutputData: Single output type (or void for multi-output)
 * - OutputTypesTuple: Tuple of output types (for address calculation)
 */
template<
    typename Registry,
    bool has_continuous_input,
    bool has_multi_input,
    typename InputData = void,
    typename InputTypesTuple = std::tuple<>,
    size_t InputCount = 0,
    typename OutputData = void,
    typename OutputTypesTuple = std::tuple<>
>
class SubscriptionProtocol {
protected:
    using SubscribeRequestType = SubscribeRequestPayload;
    using SubscribeReplyType = SubscribeReplyPayload;
    using UnsubscribeRequestType = UnsubscribeRequestPayload;
    using UnsubscribeReplyType = UnsubscribeReplyPayload;
    
    std::vector<SubscriptionState> input_subscriptions_;
    mutable std::mutex subscription_mutex_;
    
    // Reference to module's config and mailboxes (set by derived class)
    const ModuleConfig* config_{nullptr};
    RegistryMailbox<SystemRegistry>* work_mailbox_{nullptr};
    std::string module_name_;
    
    // Helper removed - work_mailbox_ is now properly typed
    
public:
    void set_config(const ModuleConfig* cfg) { config_ = cfg; }
    void set_work_mailbox(RegistryMailbox<SystemRegistry>* mbx) { work_mailbox_ = mbx; }
    void set_module_name(const std::string& name) { module_name_ = name; }
    
    /**
     * @brief Subscribe to all configured input sources
     * 
     * For multi-input modules: subscribes to each source in config_.input_sources
     * For single-input modules: subscribes using legacy config fields
     */
    void subscribe_to_all_sources() {
        static_assert(has_continuous_input || has_multi_input, 
                      "subscribe_to_all_sources() only for continuous or multi-input modules");
        
        if constexpr (has_multi_input) {
            // Multi-input: subscribe to each source in input_sources
            auto& sources = config_->input_sources();
            if (sources.empty()) {
                std::cerr << "[" << module_name_ << "] ERROR: Multi-input module but input_sources is empty!\n";
                return;
            }
            
            std::lock_guard<std::mutex> lock(subscription_mutex_);
            input_subscriptions_.resize(sources.size());
            
            for (size_t i = 0; i < sources.size(); ++i) {
                auto& source = sources[i];  // Non-const to populate input_index
                source.input_index = i;  // Save the index for unsubscribe
                subscribe_to_source_impl(config_->input_system_id(i), config_->input_instance_id(i), i);
            }
        } else if constexpr (has_continuous_input) {
            // Single-input: use config fields
            if (config_->has_single_input()) {
                input_subscriptions_.resize(1);
                subscribe_to_source_impl(config_->source_system_id(), config_->source_instance_id(), 0);
            }
        }
    }
    
    /**
     * @brief Legacy single-input subscription (backward compatible)
     */
    void subscribe_to_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "subscribe_to_source() only available for continuous input modules");
        input_subscriptions_.resize(1);
        subscribe_to_source_impl(source_system_id, source_instance_id, 0);
    }
    
    /**
     * @brief Unsubscribe from single-input source
     */
    void unsubscribe_from_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "unsubscribe_from_source() only available for continuous input modules");
        
        uint32_t our_base_addr = commrat::calculate_base_address<OutputData, OutputTypesTuple, Registry>(
            config_->system_id(), config_->instance_id());
        
        UnsubscribeRequestType request{
            .subscriber_base_addr = our_base_addr
        };
        
        // Calculate source WORK mailbox using proper RACK encoding
        uint32_t source_work_mbx;
        
        if constexpr (has_continuous_input && !has_multi_input) {
            // Single continuous input
            constexpr uint32_t source_data_type_id = Registry::template get_message_id<InputData>();
            constexpr uint8_t source_type_id = static_cast<uint8_t>(source_data_type_id & 0xFF);
            source_work_mbx = encode_address(source_type_id, source_system_id, 
                                            source_instance_id, static_cast<uint8_t>(MailboxType::WORK));
        }
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_->send(request, source_work_mbx);
    }
    
    /**
     * @brief Unsubscribe from multi-input source
     */
    /**
     * @brief Unsubscribe from multi-input source
     * 
     * Requires InputSource.input_index to be set (auto-populated during subscription)
     */
    void unsubscribe_from_multi_input_source(const MultiInputConfig::InputSource& source) {
        static_assert(has_multi_input, "unsubscribe_from_multi_input_source() only for multi-input modules");
        
        uint32_t our_base_addr = commrat::calculate_base_address<OutputData, OutputTypesTuple, Registry>(
            config_->system_id(), config_->instance_id());
        
        UnsubscribeRequestType request{
            .subscriber_base_addr = our_base_addr
        };
        
        // Use the input_index to get the correct type ID
        // Multi-input: use the input type at source.input_index
        uint32_t source_data_type_id = get_input_type_id_at_index(source.input_index);
        
        // Calculate source WORK mailbox using proper RACK encoding
        uint8_t source_type_id = static_cast<uint8_t>(source_data_type_id & 0xFF);
        uint32_t source_work_mbx = encode_address(source_type_id, source.system_id, 
                                                  source.instance_id, static_cast<uint8_t>(MailboxType::WORK));
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_->send(request, source_work_mbx);
    }
    
    /**
     * @brief Handle incoming SubscribeRequest (producer side)
     */
    template<typename SubscriberMgr>
    void handle_subscribe_request(const SubscribeRequestType& req, SubscriberMgr& sub_mgr, std::size_t output_idx = 0) {
        try {
            // req.subscriber_base_addr is the subscriber's base address
            // req.mailbox_index tells us which DATA mailbox to send to
            // Phase 7: Route to correct output-specific subscriber list
            sub_mgr.add_subscriber_to_output(req.subscriber_base_addr, req.mailbox_index, output_idx);
            // Base address from calculate_base_address() has mailbox_index=0, so OR adds the index
            // This relies on address_helpers.hpp encoding: [type][sys][inst][0x00] | [mailbox_index]
            uint32_t subscriber_data_mbx = req.subscriber_base_addr | req.mailbox_index;
            std::cout << "[" << module_name_ << "] Added subscriber to output-specific list, "
                      << "will send to DATA mailbox=0x" << std::hex << subscriber_data_mbx << std::dec << "\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = config_->period.count(),
                .success = true,
                .error_code = 0
            };
            
            // Send reply to subscriber's WORK mailbox (base + 1)
            uint32_t subscriber_work_mbx = req.subscriber_base_addr + static_cast<uint8_t>(MailboxType::WORK);
            work_mailbox_->send(reply, subscriber_work_mbx);
        } catch (...) {
            std::cout << "[" << module_name_ << "] Failed to add subscriber: 0x" << std::hex << req.subscriber_base_addr << std::dec << "\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = 0,
                .success = false,
                .error_code = 1  // Max subscribers exceeded
            };
            
            uint32_t subscriber_work_mbx = req.subscriber_base_addr + static_cast<uint8_t>(MailboxType::WORK);
            work_mailbox_->send(reply, subscriber_work_mbx);
        }
    }
    
    /**
     * @brief Handle incoming SubscribeReply (consumer side)
     */
    void handle_subscribe_reply(const SubscribeReplyType& reply) {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        
        // Phase 6.6: Mark subscription as complete
        // For now, mark the first non-replied subscription
        // TODO: Need to track which reply corresponds to which source
        for (auto& sub : input_subscriptions_) {
            if (sub.subscribed && !sub.reply_received) {
                sub.reply_received = true;
                sub.actual_period_ms = reply.actual_period_ms;
                std::cout << "[" << module_name_ << "] SubscribeReply received: "
                          << (reply.success ? "SUCCESS" : "FAILED")
                          << ", actual_period_ms=" << reply.actual_period_ms << "\n";
                return;
            }
        }
    }
    
    /**
     * @brief Handle incoming UnsubscribeRequest (producer side)
     */
    template<typename SubscriberMgr>
    void handle_unsubscribe_request(const UnsubscribeRequestType& req, SubscriberMgr& sub_mgr) {
        sub_mgr.remove_subscriber(req.subscriber_base_addr);
        
        UnsubscribeReplyType reply{.success = true};
        uint32_t subscriber_work_mbx = req.subscriber_base_addr + static_cast<uint8_t>(MailboxType::WORK);
        work_mailbox_->send(reply, subscriber_work_mbx);
    }
    
protected:
    /**
     * @brief Internal implementation: send SubscribeRequest to one source
     */
    void subscribe_to_source_impl(uint8_t source_system_id, uint8_t source_instance_id,
                                   size_t source_index) {
        // Calculate subscriber_base_addr and source_data_type_id
        uint32_t source_data_type_id;
        
        // BOTH single-input and multi-input now use OUTPUT type for base address
        // This prevents collisions (module identity = OUTPUT type)
        uint32_t subscriber_base_addr = commrat::calculate_base_address<OutputData, OutputTypesTuple, Registry>(
            config_->system_id(), config_->instance_id());
        
        // Calculate actual DATA mailbox index (after CMD mailboxes)
        constexpr uint8_t num_outputs = std::tuple_size_v<OutputTypesTuple>;
        uint8_t data_mailbox_index;
        
        if constexpr (has_multi_input) {
            // Multi-input: Specify which input index for DATA mailbox offset
            source_data_type_id = get_input_type_id_at_index(source_index);
            // DATA mailboxes start after CMD mailboxes: base + num_outputs + input_index
            data_mailbox_index = get_data_mbx_base(num_outputs) + static_cast<uint8_t>(source_index);
        } 
        else if constexpr (has_continuous_input) {
            // Single continuous input: input_index = 0 (default DATA mailbox)
            source_data_type_id = Registry::template get_message_id<InputData>();
            data_mailbox_index = get_data_mbx_base(num_outputs) + 0;
        } 
        else {
            static_assert(has_continuous_input || has_multi_input, "Invalid input configuration");
            return;
        }
        
        SubscribeRequestType request{
            .subscriber_base_addr = subscriber_base_addr,
            .mailbox_index = data_mailbox_index,  // Actual DATA mailbox index
            .requested_period_ms = config_->period.count()
        };
        
        // Calculate source WORK mailbox address using proper RACK encoding
        // Format: [type_id:8][system_id:8][instance_id:8][mailbox_index:8]
        uint8_t source_type_id = static_cast<uint8_t>(source_data_type_id & 0xFF);
        uint32_t source_work_mbx = encode_address(source_type_id, source_system_id, 
                                                   source_instance_id, static_cast<uint8_t>(MailboxType::WORK));
        
        std::cout << "[" << module_name_ << "] Sending SubscribeRequest[" << source_index 
                  << "] to source WORK mailbox " << source_work_mbx << "\n";
        
        // Retry a few times in case the producer's mailbox isn't ready yet
        int max_retries = 5;
        for (int i = 0; i < max_retries; ++i) {
            // Send subscribe request from work mailbox (SystemRegistry messages)
            auto result = work_mailbox_->send(request, source_work_mbx);
            if (result) {
                std::cout << "[" << module_name_ << "] SubscribeRequest[" << source_index 
                          << "] sent successfully\n";
                // Mark subscription as sent (reply not yet received)
                if (source_index < input_subscriptions_.size()) {
                    input_subscriptions_[source_index].subscribed = true;
                    input_subscriptions_[source_index].reply_received = false;
                }
                return;
            }
            
            if (i < max_retries - 1) {
                std::cout << "[" << module_name_ << "] Failed to send SubscribeRequest (attempt " 
                          << (i + 1) << "/" << max_retries << "), retrying...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        std::cout << "[" << module_name_ << "] Failed to send SubscribeRequest[" << source_index
                  << "] after " << max_retries << " attempts!\n";
    }
    
    /**
     * @brief Helper: Get message ID for input type at runtime index
     */
    template<size_t... Is>
    uint32_t get_input_type_id_at_index_impl(size_t index, std::index_sequence<Is...>) const {
        uint32_t result = 0;
        ((index == Is ? (result = Registry::template get_message_id<std::tuple_element_t<Is, InputTypesTuple>>(), true) : false) || ...);
        return result;
    }
    
    uint32_t get_input_type_id_at_index(size_t index) const {
        if constexpr (has_multi_input) {
            return get_input_type_id_at_index_impl(index, std::make_index_sequence<InputCount>{});
        } else {
            return Registry::template get_message_id<InputData>();
        }
    }
};

}  // namespace commrat
