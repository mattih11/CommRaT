#pragma once

#include "../message_id.hpp"
#include "../message_registry.hpp"
#include "subscription_messages.hpp"
#include "data_request_messages.hpp"

// Forward declaration for Module (breaks circular dependency)
namespace commrat {
    template<typename Registry, typename OutputDataT, typename InputModeT, typename... CommandTypes>
    class Module;
    
    template<typename... MessageDefs>
    class Mailbox;
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
 * 
 * Note: GetDataRequest/Reply and GetNextDataRequest/Reply are NOT in this registry
 * because they are type-specific (GetDataRequest<T>) and are automatically added
 * when Message::Data<T> types are registered.
 */
using SystemRegistry = MessageRegistry<
    SubscribeRequest,
    UnsubscribeRequest
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
        UnsubscribeRequest,
        UserMessageDefs...
    >;
    
// Convenience aliases that users get automatically
    template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
    using Module = commrat::Module<Type, OutputDataT, InputModeT, CommandTypes...>;
    
    // Extract underlying Mailbox type from MessageRegistry
    template<typename... MessageDefs>
    static auto extract_mailbox(MessageRegistry<MessageDefs...>*) -> commrat::Mailbox<MessageDefs...>;
    
    using Mailbox = decltype(extract_mailbox(static_cast<Type*>(nullptr)));
};

// ============================================================================
// Backward Compatibility Alias
// ============================================================================

/**
 * @brief Combine system messages with user messages into one registry
 * 
 * @deprecated Use Registry<> instead for automatic Module/Mailbox aliases
 * 
 * NOTE: GetDataRequest/Reply and GetNextDataRequest/Reply are NOT included here
 * because they are type-specific templates (GetDataRequest<T>) and are automatically
 * added when Message::Data<T> types are registered via ExpandGetDataMessages.
 */
template<typename... UserMessageDefs>
using CombinedRegistry = MessageRegistry<
    // System messages (always included)
    SubscribeRequest,
    UnsubscribeRequest,
    // User messages
    UserMessageDefs...
>;

} // namespace commrat
