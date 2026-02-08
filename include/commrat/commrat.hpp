#pragma once

/**
 * @file commrat.hpp
 * @brief Main CommRaT header - include this to get everything you need
 * 
 * This header provides:
 * - Module template (automatically includes system messages)
 * - MessageDefinition for defining messages
 * - Message prefixes and subprefixes
 * - All necessary framework components
 * 
 * Users include this + their messages header, that's it!
 */

// Core framework components
#include "commrat/messages.hpp"
#include "commrat/messaging/message_id.hpp"
#include "commrat/messaging/message_registry.hpp"
#include "commrat/messaging/message_helpers.hpp"
#include "commrat/module/io_spec.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/platform/tims_wrapper.hpp"
#include "commrat/mailbox/mailbox.hpp"
#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/registry_module.hpp"

/**
 * @namespace commrat
 * @brief CommRaT - Modern C++ Real-Time Communication Framework
 * 
 * Integrates RACK's TiMS IPC with SeRTial's reflective serialization
 * to provide compile-time safe, zero-syscall, RT-capable messaging.
 * 
 * Key Features:
 * - Compile-time message type registry for zero-overhead dispatch
 * - Template-based serialization with automatic type validation
 * - Runtime visitor pattern for dynamic message handling
 * - Zero-allocation design using SeRTial containers
 * - Automatic system message inclusion
 * - Clean payload-only user interface
 */

namespace commrat {

/**
 * @brief CommRaT Application Template - Main User-Facing Interface
 * 
 * This combines MessageRegistry with Module/Mailbox for a clean API:
 * 
 * Usage:
 *   using MyApp = CommRaT<
 *       MessageDefinition<SensorData, ...>,
 *       MessageDefinition<FilteredData, ...>
 *   >;
 *   
 *   class SensorModule : public MyApp::Module<Output<SensorData>, PeriodicInput> {
 *       SensorData process() override { ... }
 *   };
 * 
 * Advantages over raw MessageRegistry:
 * - Module template alias included (Registry::Module wasn't clear)
 * - Mailbox types available via MyApp::Mailbox<T>
 * - All application components in one namespace
 * - Clearer intent: "This is my CommRaT application definition"
 */
template<typename... MessageDefs>
class CommRaT : public MessageRegistry<MessageDefs...> {
private:
    using Registry = MessageRegistry<MessageDefs...>;

public:
    // Inherit all registry functionality
    using Registry::is_registered;
    using Registry::get_message_id;
    using Registry::serialize;
    using Registry::deserialize;
    using Registry::visit;
    using Registry::dispatch;
    using Registry::max_message_size;
    
    /**
     * @brief Module template - create modules for this application
     * 
     * Preferred user-facing API for defining modules:
     *   class MyModule : public MyApp::Module<OutputSpec, InputSpec, ...Commands> {
     *       // Your process() implementation
     *   };
     */
    template<typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
    using Module = commrat::Module<Registry, OutputSpec_, InputSpec_, CommandTypes...>;
    
    /**
     * @brief Mailbox template - create mailboxes for this application
     * 
     * For advanced use cases that need direct mailbox access:
     *   MyApp::Mailbox<SensorData> sensor_mailbox{config};
     */
    template<typename PayloadT>
    using Mailbox = RegistryMailbox<Registry>;
    
    /**
     * @brief HistoricalMailbox template - mailbox with time-synchronized getData()
     * 
     * For modules that need to synchronize inputs by timestamp (Phase 6):
     *   MyApp::HistoricalMailbox<100> history{config, tolerance};
     *   auto data = history.getData<SensorData>(timestamp);
     * 
     * @tparam HistorySize Number of messages to buffer per type
     */
    template<std::size_t HistorySize>
    using HistoricalMailbox = commrat::HistoricalMailbox<Registry, HistorySize>;
};

} // namespace commrat
