#pragma once

/**
 * @file commrat.hpp
 * @brief Main CommRaT header - include this to get the full framework
 */

#include <cstdint>

// Type aliases
using SystemId = uint32_t;
using InstanceId = uint32_t;

// Core framework components
#include <corerat/messaging/wire_message.hpp>
#include <corerat/messaging/message_id.hpp>
#include <corerat/ipc/mailbox.hpp>

// CommRaT namespace aliases for CoreRaT types
namespace commrat {
    // IPC / Mailbox
    template<typename... MessageDefs>
    using Mailbox = corerat::Mailbox<MessageDefs...>;
    template<typename T>
    using TimsMessage = corerat::WireMessage<T>;
    using TimsHeader  = corerat::WireHeader;
    using corerat::MailboxConfig;
    template<typename T>
    using MailboxResult = corerat::MailboxResult<T>;
    using corerat::MailboxError;

    // Platform — duration
    using corerat::Duration;
    using corerat::Nanoseconds;
    using corerat::Microseconds;
    using corerat::Milliseconds;
    using corerat::Seconds;
    using corerat::Minutes;
    using corerat::Hours;
    using namespace corerat::literals;

    // Platform — time
    using corerat::Timestamp;
    using corerat::Time;

    // Platform — threading
    using corerat::Thread;
    using corerat::Mutex;
    using corerat::SharedMutex;
    using corerat::ConditionVariable;
    using corerat::Lock;
    using corerat::UniqueLock;
    using corerat::SharedLock;
    using corerat::UniqueLockShared;
    using corerat::ThreadPriority;
    using corerat::ThreadConfig;
    using corerat::SchedulingPolicy;
    using corerat::CvStatus;
} // namespace commrat
#include "commrat/messaging/message_registry.hpp"
#include "commrat/messaging/message_helpers.hpp"
#include "commrat/module/io/io_spec.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/module2.hpp"
#include "commrat/meta/introspection_helper.hpp"
#include "commrat/module/helpers/type_name.hpp"
#include "commrat/module/helpers/address_helpers.hpp"

// Forward-declare Launcher so MyApp::Launcher is a valid alias.
// Include <commrat/launcher/launcher.hpp> to use it.
namespace commrat { template<typename App> class Launcher; }

/**
 * @namespace commrat
 * @brief CommRaT - C++20 Real-Time Communication Framework
 * 
 * Compile-time message registry, zero-allocation messaging, automatic
 * system message inclusion, and clean payload-only user interface.
 */

namespace commrat {

/**
 * @brief CommRaT Application Template
 * 
 * Primary entry point. Combines MessageRegistry with Module/Mailbox factories.
 * Automatically includes system messages (Subscribe, Unsubscribe).
 * 
 * @tparam MessageDefs Message definitions (MessageDefinition<PayloadType, ...>)
 * 
 * @code
 * using MyApp = CommRaT<Message::Data<SensorData>, Message::Data<FilteredData>>;
 * class Sensor : public MyApp::Module2<Output<SensorData>, Period<Milliseconds(10)>> { ... };
 * @endcode
 */
template<typename... MessageDefs>
class CommRaT : public MessageRegistry<MessageDefs..., SubscribeRequest, UnsubscribeRequest> {
public:  // Make Registry public so TypedMailbox can access it
    using Registry = MessageRegistry<MessageDefs..., SubscribeRequest, UnsubscribeRequest>;
    
    // Verify system messages are registered
    static_assert(Registry::template is_registered<SubscribeRequestPayload>, 
                  "SubscribeRequestPayload must be in registry");
    static_assert(Registry::template is_registered<SubscribeReplyPayload>, 
                  "SubscribeReplyPayload must be in registry");
    static_assert(Registry::template is_registered<UnsubscribeRequestPayload>, 
                  "UnsubscribeRequestPayload must be in registry");
    static_assert(Registry::template is_registered<UnsubscribeReplyPayload>, 
                  "UnsubscribeReplyPayload must be in registry");

public:
    // User-defined messages only (for introspection of user types)
    using UserRegistry = MessageRegistry<MessageDefs...>;
    
    // System registry access (all system messages in main registry now)
    struct System {
        // System message payload types
        using PayloadTypes = std::tuple<
            SubscribeRequestPayload,
            SubscribeReplyPayload,
            UnsubscribeRequestPayload,
            UnsubscribeReplyPayload
        >;
        
        // Helper to get message ID from main registry
        template<typename T>
        static constexpr uint32_t get_message_id() {
            return Registry::template get_message_id<T>();
        }
        
        // System message definitions tuple (for introspection)
        using SystemMessageDefs = std::tuple<
            SubscribeRequest,
            typename SubscribeRequest::ReplyMessageDef,
            UnsubscribeRequest,
            typename UnsubscribeRequest::ReplyMessageDef
        >;
        
        // Extract payload types from message definitions
        using SystemPayloads = std::tuple<
            typename SubscribeRequest::Payload,
            typename SubscribeRequest::ReplyMessageDef::Payload,
            typename UnsubscribeRequest::Payload,
            typename UnsubscribeRequest::ReplyMessageDef::Payload
        >;
        
        // WorkMailbox - unrestricted Mailbox that can send/receive any registered type.
        // Needs full registry access because it handles subscription protocol
        // AND GetData/GetNextData RPC (template-parameterized payload types).
        using WorkMailbox = MailboxFor<Registry>;
    };
    
    // Inherit all registry functionality
    using Registry::is_registered;
    using Registry::get_message_id;
    using Registry::serialize;
    using Registry::deserialize;
    using Registry::visit;
    using Registry::dispatch;
    using Registry::max_message_size;
    
    // Expose payload types tuple for introspection
    using payload_types = typename Registry::PayloadTypes;
    
    /**
     * @brief Old Module template (TODO: reactivate with Module2 I/O architecture)
     */
    // TODO reactivate
    // template<typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
    // using Module = commrat::Module2<Registry, OutputSpec_, InputSpec_, CommandTypes...>;
    
    /**
     * @brief Mailbox template for direct mailbox access (advanced)
     * @tparam PayloadT Message payload type (must be registered)
     */
    template<typename PayloadT>
    using Mailbox = commrat::Mailbox<MessageDefs..., SubscribeRequest, UnsubscribeRequest>;
    
    /**
     * @brief Module2 template bound to this application
     * @tparam IOSpecs... I/O specifications (Output<T>, Input<T>, Period<D>, SyncedInput<T>)
     */
    template<typename... IOSpecs>
    using Module2 = commrat::Module2<CommRaT, IOSpecs...>;
    
    /**
     * @brief HistoricalMailbox template - timestamp-synchronized get_data()
     * @tparam HistorySize Number of messages to buffer per type
     */
    //TODO Check about that.
    //template<std::size_t HistorySize>
    //using HistoricalMailbox = commrat::HistoricalMailbox<Registry, HistorySize>;
    
    /**
     * @brief Compile-time mailbox address for a known payload type T.
     *
     * Usage:  constexpr auto addr = MyApp::get_address<TemperatureData>(10, 1);
     */
    template<typename T>
    static constexpr uint32_t get_address(uint8_t system_id, uint8_t instance_id) {
        return calculate_base_address<T, std::tuple<T>, Registry>(system_id, instance_id);
    }

    /**
     * @brief Runtime mailbox address lookup by type name string.
     *
     * Iterates all registered payload types, compares TypeName<T>::value to
     * type_name, and returns the base mailbox address for the first match.
     *
     * Returns std::nullopt if type_name is not registered.
     *
     * Usage:  auto addr = MyApp::get_address("TemperatureData", 10, 1);
     */
    static std::optional<uint32_t> get_address(std::string_view type_name,
                                                uint8_t system_id,
                                                uint8_t instance_id) {
        return get_address_impl(type_name, system_id, instance_id,
                                static_cast<payload_types*>(nullptr));
    }

    /**
     * @brief Get the registered string name for payload type T.
     *
     * Returns the same string as TypeName<T>::value (reflectcpp type name).
     */
    template<typename T>
    static constexpr auto get_type_name() {
        return TypeName<T>::value;
    }

    /**
     * @brief Invoke a generic callable for the payload type matching type_name.
     *
     * Calls fn<T>() for the registered payload type whose name equals type_name.
     * Returns true if a match was found and fn was called, false otherwise.
     *
     * Usage:
     *   MyApp::visit_type("TemperatureData", []<typename T>() {
     *       // T is TemperatureData here
     *       auto bridge = std::make_unique<BridgeModule<MyApp, T>>(...);
     *   });
     */
    template<typename Fn>
    static bool visit_type(std::string_view type_name, Fn&& fn) {
        return visit_type_impl(type_name, std::forward<Fn>(fn),
                               static_cast<payload_types*>(nullptr));
    }

private:
    template<typename... Ts>
    static std::optional<uint32_t> get_address_impl(std::string_view name,
                                                     uint8_t sys, uint8_t inst,
                                                     std::tuple<Ts...>*) {
        std::optional<uint32_t> result;
        ((TypeName<Ts>::value == std::string_view(name.data(), name.size())
            ? (result = calculate_base_address<Ts, std::tuple<Ts>, Registry>(sys, inst), true)
            : false) || ...);
        return result;
    }

    template<typename Fn, typename... Ts>
    static bool visit_type_impl(std::string_view name, Fn&& fn, std::tuple<Ts...>*) {
        return ((TypeName<Ts>::value == std::string_view(name.data(), name.size())
                    ? (fn.template operator()<Ts>(), true)
                    : false) || ...);
    }

public:
    /**
     * @brief Introspection helper - export message schemas to JSON/YAML/etc.
     */
    using Introspection = IntrospectionHelper<CommRaT>;

    /**
     * @brief Config-driven launcher - load and start modules from YAML/JSON.
     * Usage: MyApp::Launcher::run(argc, argv, [](auto& l) { l.register_module<T>("name"); });
     */
    using Launcher = commrat::Launcher<CommRaT>;
};

} // namespace commrat
