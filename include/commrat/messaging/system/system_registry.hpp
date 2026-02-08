#pragma once

#include "../message_id.hpp"
#include "../message_registry.hpp"
#include "subscription_messages.hpp"

// Forward declaration for Module (breaks circular dependency)
namespace commrat {
    template<typename Registry, typename OutputDataT, typename InputModeT, typename... CommandTypes>
    class Module;
    
    template<typename Registry>
    class RegistryMailbox;
}

namespace commrat {

// ============================================================================
// System Message Registry
// ============================================================================

/**
 * @brief Framework system messages that are always included
 * 
 * These messages are used by the framework for subscription protocol
 * and other internal communication. Users don't need to manually include these.
 */
using SystemRegistry = MessageRegistry<
    SubscribeRequest,
    SubscribeReply,
    UnsubscribeRequest,
    UnsubscribeReply
>;

// ============================================================================
// Registry Builder - Creates Registry with Convenience Aliases
// ============================================================================

/**
 * @brief Build a complete registry with automatic Module and Mailbox aliases
 * 
 * This class wraps MessageRegistry and provides convenient type aliases
 * so users never need to write template aliases themselves.
 * 
 * Usage:
 * @code
 * using MyApp = Registry<
 *     Message::Data<TemperatureData>,
 *     Message::Command<ResetCmd>
 * >;
 * 
 * // Automatically provides:
 * class Sensor : public MyApp::Module<TemperatureData, PeriodicInput> { ... };
 * MyApp::Mailbox mbx(config);
 * @endcode
 */
template<typename... UserMessageDefs>
class Registry {
public:
    // The actual MessageRegistry type
    using Type = MessageRegistry<
        SubscribeRequest,
        SubscribeReply,
        UnsubscribeRequest,
        UnsubscribeReply,
        UserMessageDefs...
    >;
    
    // Convenience aliases that users get automatically
    template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
    using Module = commrat::Module<Type, OutputDataT, InputModeT, CommandTypes...>;
    
    using Mailbox = commrat::RegistryMailbox<Type>;
};

// ============================================================================
// Backward Compatibility Alias
// ============================================================================

/**
 * @brief Combine system messages with user messages into one registry
 * 
 * @deprecated Use Registry<> instead for automatic Module/Mailbox aliases
 */
template<typename... UserMessageDefs>
using CombinedRegistry = MessageRegistry<
    // System messages (always included)
    SubscribeRequest,
    SubscribeReply,
    UnsubscribeRequest,
    UnsubscribeReply,
    // User messages
    UserMessageDefs...
>;

} // namespace commrat
