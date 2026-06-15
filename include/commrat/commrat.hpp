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
#include "commrat/introspection/introspection_helper.hpp"

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
     * @brief Introspection helper - export message schemas to JSON/YAML/etc.
     */
    using Introspection = IntrospectionHelper<CommRaT>;
};

} // namespace commrat
