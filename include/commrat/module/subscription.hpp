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
 * @brief Subscriber management mixin
 * 
 * Provides add/remove subscriber functionality with thread-safe access.
 * Intended to be used as CRTP base or member in Module class.
 */
class SubscriberManager {
protected:
    std::vector<uint32_t> subscribers_;
    mutable std::mutex subscribers_mutex_;
    size_t max_subscribers_{100};  // Default, overridden by config
    
public:
    void set_max_subscribers(size_t max) { max_subscribers_ = max; }
    
    void add_subscriber(uint32_t subscriber_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        
        // Check if already subscribed
        if (std::find(subscribers_.begin(), subscribers_.end(), subscriber_id) != subscribers_.end()) {
            return;
        }
        
        // Check capacity
        if (subscribers_.size() >= max_subscribers_) {
            throw std::runtime_error("Maximum subscribers reached");
        }
        
        subscribers_.push_back(subscriber_id);
    }
    
    void remove_subscriber(uint32_t subscriber_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), subscriber_id),
            subscribers_.end()
        );
    }
    
    size_t subscriber_count() const {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        return subscribers_.size();
    }
    
    std::vector<uint32_t> get_subscribers() const {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        return subscribers_;
    }
    
    // For Publisher access (non-const, caller must lock)
    std::vector<uint32_t>& get_subscribers() {
        return subscribers_;
    }
    
    std::mutex& get_subscribers_mutex() {
        return subscribers_mutex_;
    }
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
    
    /**
     * @brief Calculate base mailbox address for this module
     * Uses output type info to determine address, same as Module class
     */
    static constexpr uint32_t calculate_base_address(uint8_t system_id, uint8_t instance_id) {
        using BaseType = std::conditional_t<
            std::is_void_v<OutputData>,
            std::tuple_element_t<0, OutputTypesTuple>,
            OutputData
        >;
        constexpr uint32_t data_type_id = Registry::template get_message_id<BaseType>();
        constexpr uint16_t data_type_id_low = static_cast<uint16_t>(data_type_id & 0xFFFF);
        return (static_cast<uint32_t>(data_type_id_low) << 16) | (system_id << 8) | instance_id;
    }
    
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
            if (config_->input_sources.empty()) {
                std::cerr << "[" << module_name_ << "] ERROR: Multi-input module but input_sources is empty!\n";
                return;
            }
            
            std::lock_guard<std::mutex> lock(subscription_mutex_);
            input_subscriptions_.resize(config_->input_sources.size());
            
            for (size_t i = 0; i < config_->input_sources.size(); ++i) {
                const auto& source = config_->input_sources[i];
                subscribe_to_source_impl(source.system_id, source.instance_id, 
                                          source.source_primary_output_type_id, i);
            }
        } else if constexpr (has_continuous_input) {
            // Single-input: use legacy config fields
            if (config_->source_system_id && config_->source_instance_id) {
                input_subscriptions_.resize(1);
                subscribe_to_source_impl(*config_->source_system_id, *config_->source_instance_id,
                                          config_->source_primary_output_type_id, 0);
            }
        }
    }
    
    /**
     * @brief Legacy single-input subscription (backward compatible)
     */
    void subscribe_to_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "subscribe_to_source() only available for continuous input modules");
        input_subscriptions_.resize(1);
        subscribe_to_source_impl(source_system_id, source_instance_id, 
                                  config_->source_primary_output_type_id, 0);
    }
    
    /**
     * @brief Unsubscribe from single-input source
     */
    void unsubscribe_from_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "unsubscribe_from_source() only available for continuous input modules");
        
        uint32_t our_base_addr = calculate_base_address(config_->system_id, config_->instance_id);
        
        UnsubscribeRequestType request{
            .subscriber_mailbox_id = our_base_addr
        };
        
        // Calculate source WORK mailbox
        uint32_t source_base;
        uint32_t source_work_mbx;
        
        if constexpr (has_continuous_input && !has_multi_input) {
            // Single continuous input
            constexpr uint32_t source_data_type_id = Registry::template get_message_id<InputData>();
            constexpr uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
            source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | (source_system_id << 8) | source_instance_id;
            source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        }
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_->send(request, source_work_mbx);
    }
    
    /**
     * @brief Unsubscribe from multi-input source
     */
    void unsubscribe_from_multi_input_source(const InputSource& source) {
        static_assert(has_multi_input, "unsubscribe_from_multi_input_source() only for multi-input modules");
        
        uint32_t our_base_addr = calculate_base_address(config_->system_id, config_->instance_id);
        
        UnsubscribeRequestType request{
            .subscriber_mailbox_id = our_base_addr
        };
        
        // Use source_primary_output_type_id if provided, otherwise we can't compute address
        // In practice, multi-input always requires this field
        if (!source.source_primary_output_type_id.has_value()) {
            std::cerr << "[" << module_name_ << "] ERROR: Cannot unsubscribe - source_primary_output_type_id not set!\n";
            return;
        }
        
        uint32_t source_data_type_id = *source.source_primary_output_type_id;
        uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
        uint32_t source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | 
                               (source.system_id << 8) | source.instance_id;
        uint32_t source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_->send(request, source_work_mbx);
    }
    
    /**
     * @brief Handle incoming SubscribeRequest (producer side)
     */
    template<typename SubscriberMgr>
    void handle_subscribe_request(const SubscribeRequestType& req, SubscriberMgr& sub_mgr) {
        try {
            // req.subscriber_mailbox_id is the subscriber's base address
            sub_mgr.add_subscriber(req.subscriber_mailbox_id);
            uint32_t subscriber_data_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::DATA);
            std::cout << "[" << module_name_ << "] Added subscriber base=" << req.subscriber_mailbox_id 
                      << ", will send to DATA mailbox=" << subscriber_data_mbx
                      << " (total subscribers: " << sub_mgr.subscriber_count() << ")\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = config_->period.count(),
                .success = true,
                .error_code = 0
            };
            
            // Send reply to subscriber's WORK mailbox (base + 1)
            uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
            work_mailbox_->send(reply, subscriber_work_mbx);
        } catch (...) {
            std::cout << "[" << module_name_ << "] Failed to add subscriber: " << req.subscriber_mailbox_id << "\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = 0,
                .success = false,
                .error_code = 1  // Max subscribers exceeded
            };
            
            uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
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
        sub_mgr.remove_subscriber(req.subscriber_mailbox_id);
        
        UnsubscribeReplyType reply{.success = true};
        uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
        work_mailbox_->send(reply, subscriber_work_mbx);
    }
    
protected:
    /**
     * @brief Internal implementation: send SubscribeRequest to one source
     */
    void subscribe_to_source_impl(uint8_t source_system_id, uint8_t source_instance_id,
                                   std::optional<uint32_t> source_primary_output_type_id,
                                   size_t source_index) {
        // For multi-input: Calculate base address using the INPUT TYPE we're subscribing to
        // This ensures producers send to the correct input-specific DATA mailbox
        uint32_t subscriber_base_addr;
        
        if constexpr (has_multi_input) {
            // Multi-input: use the input type ID at source_index for subscriber base address
            uint32_t input_type_id = get_input_type_id_at_index(source_index);
            uint16_t input_type_id_low = static_cast<uint16_t>(input_type_id & 0xFFFF);
            subscriber_base_addr = (static_cast<uint32_t>(input_type_id_low) << 16) | 
                                    (config_->system_id << 8) | config_->instance_id;
        } else {
            // Single-input: use output type for base address (backward compatible)
            subscriber_base_addr = calculate_base_address(config_->system_id, config_->instance_id);
        }
        
        SubscribeRequestType request{
            .subscriber_mailbox_id = subscriber_base_addr,  // Input-type-specific base for multi-input
            .requested_period_ms = config_->period.count()
        };
        
        // Calculate source module's WORK mailbox address
        uint32_t source_data_type_id;
        
        // PRIORITY 1: Multi-input uses input type at source_index
        if constexpr (has_multi_input) {
            // Multi-input: use the input type at source_index (what WE want for THIS input)
            source_data_type_id = get_input_type_id_at_index(source_index);
        } 
        // PRIORITY 2: Single continuous input uses InputData type
        else if constexpr (has_continuous_input) {
            // Single-input: use InputData type (what WE want to subscribe to)
            source_data_type_id = Registry::template get_message_id<InputData>();
        } 
        // PRIORITY 3: Fallback to provided primary output type ID
        else if (source_primary_output_type_id) {
            // Multi-output producer: use the provided primary output type ID
            source_data_type_id = *source_primary_output_type_id;
        } 
        else {
            static_assert(has_continuous_input || has_multi_input, "Invalid input configuration");
            return;
        }
        
        uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
        uint32_t source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | 
                                (source_system_id << 8) | source_instance_id;
        uint32_t source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        
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
