#pragma once

#include "commrat/module/module_config.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/platform/threading.hpp"
#include <vector>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <typeinfo>

namespace commrat {

/**
 * @brief CRTP mixin for multi-output mailbox and subscriber management
 * 
 * Provides infrastructure for modules with multiple output types:
 * - Per-output MailboxSet access (CMD/WORK/PUBLISH mailboxes)
 * - Per-output subscriber lists with type-based routing
 * - Per-output work threads for independent subscription handling
 * - Mailbox lifecycle management (start/stop)
 * 
 * Only active when Derived::use_mailbox_sets == true (multi-output modules).
 * Single-output modules use the traditional mailbox accessors in Module.
 * 
 * @tparam Derived The Module class (CRTP pattern)
 * @tparam UserRegistry The application's MessageRegistry
 * @tparam OutputTypesTuple std::tuple<OutputType1, OutputType2, ...>
 */
template<typename Derived, typename UserRegistry, typename OutputTypesTuple>
class MultiOutputManager {
protected:
    // CRTP: Get reference to derived Module class
    Derived& derived() { return static_cast<Derived&>(*this); }
    const Derived& derived() const { return static_cast<const Derived&>(*this); }
    
    // ========================================================================
    // Multi-Output Subscriber Management
    // ========================================================================
    
    /**
     * @brief Per-output subscriber lists: output_subscribers_[output_index][subscriber_id]
     * 
     * Each output type maintains its own subscriber list for type-safe publishing.
     * Subscriptions are routed based on the subscriber's expected message type ID.
     */
    std::vector<std::vector<uint32_t>> output_subscribers_;
    mutable Mutex output_subscribers_mutex_;  // Protects output_subscribers_
    
    /**
     * @brief Initialize per-output subscriber lists
     * 
     * Called from Module constructor to resize vector for multi-output modules.
     */
    void initialize_output_subscribers() {
        constexpr std::size_t num_outputs = std::tuple_size_v<OutputTypesTuple>;
        output_subscribers_.resize(num_outputs);
    }
    
    /**
     * @brief Add subscriber to correct output-specific list
     * 
     * Extracts type ID from subscriber base address and routes to the
     * appropriate output's subscriber list.
     * 
     * @param subscriber_base_addr Subscriber's base mailbox address
     *        Format: [type_id_low:16][system:8][instance:8]
     */
    void add_subscriber_to_output(uint32_t subscriber_base_addr) {
        // Extract type ID from subscriber's base address
        uint16_t subscriber_type_id_low = static_cast<uint16_t>((subscriber_base_addr >> 16) & 0xFFFF);
        
        // Find matching output index
        std::size_t output_idx = find_output_index_by_type_id(subscriber_type_id_low);
        
        constexpr std::size_t num_outputs = std::tuple_size_v<OutputTypesTuple>;
        if (output_idx < num_outputs) {
            Lock lock(output_subscribers_mutex_);
            auto& subs = output_subscribers_[output_idx];
            
            // Check if already subscribed
            if (std::find(subs.begin(), subs.end(), subscriber_base_addr) == subs.end()) {
                subs.push_back(subscriber_base_addr);
                std::cout << "[" << derived().config_.name << "] Added subscriber " << subscriber_base_addr 
                          << " to output[" << output_idx << "] (total: " << subs.size() << ")\n";
            }
        } else {
            std::cerr << "[" << derived().config_.name << "] ERROR: No matching output type for subscriber type ID " 
                      << subscriber_type_id_low << "\n";
        }
    }

public:
    /**
     * @brief Get subscribers for specific output index
     * 
     * Thread-safe accessor for per-output subscriber lists.
     * Used by Publisher to send messages only to relevant subscribers.
     * 
     * @param output_idx Output index (0-based)
     * @return Copy of subscriber list for that output
     */
    std::vector<uint32_t> get_output_subscribers(std::size_t output_idx) const {
        Lock lock(output_subscribers_mutex_);
        if (output_idx < output_subscribers_.size()) {
            return output_subscribers_[output_idx];
        }
        return {};
    }

private:
    /**
     * @brief Find output index by type ID (lower 16 bits of message ID)
     * 
     * Searches all output types to find which one matches the given type ID.
     * 
     * @param type_id_low Lower 16 bits of message ID
     * @return Output index if found, or num_output_types if not found
     */
    template<std::size_t... Is>
    std::size_t find_output_index_by_type_id_impl(uint16_t type_id_low, std::index_sequence<Is...>) const {
        constexpr std::size_t num_outputs = std::tuple_size_v<OutputTypesTuple>;
        std::size_t result = num_outputs;  // Invalid index by default
        
        // Check each output type's message ID using fold expression
        ((check_output_type_match<Is>(type_id_low, result)) || ...);
        
        return result;
    }
    
    /**
     * @brief Check if specific output type matches the given type ID
     * 
     * @tparam Index Output index to check
     * @param type_id_low Type ID to match against
     * @param result Output parameter - set to Index if matched
     * @return true if matched (stops fold expression), false otherwise
     */
    template<std::size_t Index>
    bool check_output_type_match(uint16_t type_id_low, std::size_t& result) const {
        using OutputType = std::tuple_element_t<Index, OutputTypesTuple>;
        constexpr uint32_t output_msg_id = UserRegistry::template get_message_id<OutputType>();
        constexpr uint16_t output_type_id_low = static_cast<uint16_t>(output_msg_id & 0xFFFF);
        
        if (output_type_id_low == type_id_low) {
            result = Index;
            return true;  // Found match, stop searching
        }
        return false;  // Continue searching
    }
    
protected:
    /**
     * @brief Public wrapper for find_output_index_by_type_id_impl
     */
    std::size_t find_output_index_by_type_id(uint16_t type_id_low) const {
        constexpr std::size_t num_outputs = std::tuple_size_v<OutputTypesTuple>;
        return find_output_index_by_type_id_impl(type_id_low, std::make_index_sequence<num_outputs>{});
    }
    
    // ========================================================================
    // Multi-Output Work Threads
    // ========================================================================
    
    /**
     * @brief Per-output work threads: one thread per MailboxSet
     * 
     * Each output type gets its own work thread to handle subscription protocol
     * independently on its dedicated WORK mailbox.
     */
    std::vector<std::thread> output_work_threads_;
    
    /**
     * @brief Spawn all output work threads
     * 
     * Creates one thread per output type using fold expression.
     * Called from Module::start().
     */
    template<std::size_t... Is>
    void spawn_all_output_work_threads(std::index_sequence<Is...>) {
        // Reserve space for all threads
        output_work_threads_.reserve(sizeof...(Is));
        
        // Spawn one thread per output type using fold expression
        (void)std::initializer_list<int>{
            (spawn_output_work_thread<Is>(), 0)...
        };
    }
    
    /**
     * @brief Spawn work thread for a specific output index
     * 
     * @tparam Index Output index (0-based)
     */
    template<std::size_t Index>
    void spawn_output_work_thread() {
        output_work_threads_.emplace_back([this]() {
            derived().template output_work_loop<Index>();
        });
    }
    
    /**
     * @brief Join all output work threads
     * 
     * Called from Module::stop() to wait for all work threads to finish.
     */
    void join_output_work_threads() {
        std::cout << "[" << derived().config_.name << "] Joining " << output_work_threads_.size() 
                  << " output work threads...\n";
        for (auto& thread : output_work_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        output_work_threads_.clear();
    }
    
    /**
     * @brief Work loop for a specific output index
     * 
     * Each MailboxSet gets its own work thread to handle subscriptions
     * independently. This allows consumers to subscribe to specific output
     * types without interference.
     * 
     * Main processing loop for WORK mailbox for a specific output.
     * 
     * Handles subscription protocol messages on the WORK mailbox for each
     * output type independently. Runs in its own thread.
     * 
     * @tparam Index Output index (0-based)
     */
    template<std::size_t Index>
    void output_work_loop() {
        using OutputType = std::tuple_element_t<Index, OutputTypesTuple>;
        uint32_t work_mailbox_addr = commrat::get_mailbox_address<OutputType, OutputTypesTuple, UserRegistry>(
            derived().config_.system_id, derived().config_.instance_id, MailboxType::WORK);
        
        std::cout << "[" << derived().config_.name << "] output_work_loop[" << Index << "] started for "
                  << typeid(OutputType).name() << ", listening on WORK mailbox " 
                  << work_mailbox_addr << "\n" << std::flush;
        
        auto& work_mbx = derived().template get_work_mailbox<Index>();
        
        while (derived().running_) {
            std::cout << "[" << derived().config_.name << "] output_work_loop[" << Index << "]: waiting for message...\n" << std::flush;
            auto visitor = [this](auto&& tims_msg) {
                auto& msg = tims_msg.payload;
                using MsgType = std::decay_t<decltype(msg)>;
                
                if constexpr (std::is_same_v<MsgType, SubscribeRequestType>) {
                    std::cout << "[" << derived().config_.name << "] output_work_loop[" << Index 
                              << "] Handling SubscribeRequest\n";
                    derived().handle_subscribe_request(msg);
                } else if constexpr (std::is_same_v<MsgType, SubscribeReplyType>) {
                    std::cout << "[" << derived().config_.name << "] output_work_loop[" << Index 
                              << "] Handling SubscribeReply\n";
                    derived().handle_subscribe_reply(msg);
                } else if constexpr (std::is_same_v<MsgType, UnsubscribeRequestType>) {
                    std::cout << "[" << derived().config_.name << "] output_work_loop[" << Index 
                              << "] Handling UnsubscribeRequest\n";
                    derived().handle_unsubscribe_request(msg);
                }
            };
            
            work_mbx.receive_any(visitor);
        }
        
        std::cout << "[" << derived().config_.name << "] output_work_loop[" << Index << "] ended\n";
    }
    
    // ========================================================================
    // Multi-Output Mailbox Set Lifecycle
    // ========================================================================
    
    /**
     * @brief Start all MailboxSets (CMD/WORK/PUBLISH mailboxes)
     * 
     * Called from Module::start() to initialize all multi-output mailboxes.
     */
    template<std::size_t... Is>
    void start_all_mailbox_sets(std::index_sequence<Is...>) {
        (void)std::initializer_list<int>{
            (start_mailbox_set<Is>(), 0)...
        };
    }
    
    /**
     * @brief Start a single MailboxSet (CMD/WORK/PUBLISH)
     * 
     * @tparam Index Output index (0-based)
     */
    template<std::size_t Index>
    void start_mailbox_set() {
        auto& cmd = derived().template get_cmd_mailbox<Index>();
        auto cmd_result = cmd.start();
        if (!cmd_result) {
            throw std::runtime_error("[Module] Failed to start CMD mailbox for output " + std::to_string(Index));
        }
        
        auto& work = derived().template get_work_mailbox<Index>();
        auto work_result = work.start();
        if (!work_result) {
            throw std::runtime_error("[Module] Failed to start WORK mailbox for output " + std::to_string(Index));
        }
        
        auto& publish = derived().template get_publish_mailbox_public<Index>();
        auto publish_result = publish.start();
        if (!publish_result) {
            throw std::runtime_error("[Module] Failed to start PUBLISH mailbox for output " + std::to_string(Index));
        }
    }
    
    /**
     * @brief Stop all MailboxSets
     * 
     * Called from Module::stop() to shut down all multi-output mailboxes.
     */
    template<std::size_t... Is>
    void stop_all_mailbox_sets(std::index_sequence<Is...>) {
        (void)std::initializer_list<int>{
            (stop_mailbox_set<Is>(), 0)...
        };
    }
    
    /**
     * @brief Stop a single MailboxSet (CMD/WORK/PUBLISH)
     * 
     * @tparam Index Output index (0-based)
     */
    template<std::size_t Index>
    void stop_mailbox_set() {
        derived().template get_cmd_mailbox<Index>().stop();
        derived().template get_work_mailbox<Index>().stop();
        derived().template get_publish_mailbox_public<Index>().stop();
    }
    
    // ========================================================================
    // Multi-Output Mailbox Accessors
    // ========================================================================
    
    /**
     * @brief Get specific MailboxSet by index
     * 
     * @tparam Index Output index (0-based)
     * @return Reference to MailboxSet<UserRegistry, OutputType, CommandTypes...>
     */
    template<std::size_t Index>
    auto& get_mailbox_set() {
        return std::get<Index>(derived().mailbox_infrastructure_);
    }
    
    /**
     * @brief Get CMD mailbox for specific output index
     * 
     * @tparam Index Output index (0-based)
     * @return Reference to CMD mailbox for that output type
     */
    template<std::size_t Index>
    auto& get_cmd_mailbox() {
        return *std::get<Index>(derived().mailbox_infrastructure_).cmd;
    }
    
    /**
     * @brief Get WORK mailbox for specific output index
     * 
     * @tparam Index Output index (0-based)
     * @return Reference to WORK mailbox for that output type
     */
    template<std::size_t Index>
    auto& get_work_mailbox() {
        return *std::get<Index>(derived().mailbox_infrastructure_).work;
    }
    
    /**
     * @brief Get PUBLISH mailbox for specific output index (internal)
     * 
     * @tparam Index Output index (0-based)
     * @return Reference to PUBLISH mailbox for that output type
     */
    template<std::size_t Index>
    auto& get_publish_mailbox() {
        return *std::get<Index>(derived().mailbox_infrastructure_).publish;
    }
    
public:
    /**
     * @brief Get PUBLISH mailbox for specific output index (public accessor)
     * 
     * Public accessor needed by Publisher to access per-output mailboxes.
     * 
     * @tparam Index Output index (0-based)
     * @return Reference to PUBLISH mailbox for that output type
     */
    template<std::size_t Index>
    auto& get_publish_mailbox_public() {
        return *std::get<Index>(derived().mailbox_infrastructure_).publish;
    }
};

}  // namespace commrat
