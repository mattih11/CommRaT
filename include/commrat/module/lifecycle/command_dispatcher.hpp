/**
 * @file command_dispatcher.hpp
 * @brief Command dispatch mixin for Module
 * 
 * Extracted from registry_module.hpp Phase 3.
 * Handles user command dispatch to on_command() handlers.
 */

#pragma once

#include <iostream>
#include <string>

namespace commrat {

/**
 * @brief Command dispatcher mixin
 * 
 * Provides command_loop() and handle_user_command() for modules that accept
 * user commands via the CMD mailbox.
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 * @tparam CommandTypes Variadic pack of command payload types
 */
template<typename ModuleType, typename... CommandTypes>
class CommandDispatcher {
protected:
    /**
     * @brief Command loop - receives and dispatches user commands
     * 
     * Runs in a dedicated thread, blocking on CMD mailbox receives.
     * Routes commands to on_command() handlers based on payload type.
     */
    void command_loop() {
        auto& module = static_cast<ModuleType&>(*this);
        std::cout << "[" << module.config_.name << "] command_loop started\n";
        
        while (module.running_) {
            // Use receive_any with visitor pattern on cmd_mailbox
            // BLOCKING receive - waits indefinitely for user commands
            auto visitor = [&module](auto&& tims_msg) {
                // tims_msg is TimsMessage<PayloadT>, extract payload
                auto& msg = tims_msg.payload;
                // TODO: do we need that using MsgType = std::decay_t<decltype(msg)>; here for SFINAE in handle_user_command?
                // Maybe not since we have static_assert there already?
                using MsgType = std::decay_t<decltype(msg)>;
                
                std::cout << "[" << module.config_.name << "] Received command in command_loop\n";
                
                // Handle user command types only
                module.handle_user_command(msg);
            };
            
            // BLOCKING receive on command mailbox (no timeout)
            module.cmd_mailbox().receive_any(visitor);
        }
        
        std::cout << "[" << module.config_.name << "] command_loop ended\n";
    }
    
    /**
     * @brief Dispatch user command to on_command handler
     * 
     * @tparam CmdT Command payload type
     * @param cmd Command payload
     */
    template<typename CmdT>
    void handle_user_command(const CmdT& cmd) {
        auto& module = static_cast<ModuleType&>(*this);
        
        // Check if this is one of our declared CommandTypes
        if constexpr ((std::is_same_v<CmdT, CommandTypes> || ...)) {
            module.on_command(cmd);
        }
        // Otherwise ignore (not in our command list)
    }
    
    /**
     * @brief Handle a specific command type (override in derived class)
     * 
     * Default implementation does nothing. Derived classes should provide
     * overloads for each command type they support:
     * 
     *   void on_command(const ResetCmd& cmd) override { ... }
     *   void on_command(const CalibrateCmd& cmd) override { ... }
     * 
     * @tparam CmdT Command payload type
     * @param cmd Command payload
     */
    template<typename CmdT>
    void on_command(const CmdT& cmd) {
        // Default: no-op - override in derived classes for specific CommandTypes
        (void)cmd; // Suppress unused parameter warning
    }
};

} // namespace commrat
