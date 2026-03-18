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

#include <cstdint>

// Type aliases
using SystemId = uint32_t;
using InstanceId = uint32_t;

// Core framework components
#include "commrat/messages.hpp"
#include "commrat/messaging/message_id.hpp"
#include "commrat/messaging/message_registry.hpp"
#include "commrat/messaging/message_helpers.hpp"
#include "commrat/module/io/io_spec.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/platform/tims_wrapper.hpp"
#include "commrat/mailbox/mailbox.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/module2.hpp"
#include "commrat/introspection/introspection_helper.hpp"

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
 * This is the primary entry point for defining a CommRaT application.
 * Combines MessageRegistry with Module/Mailbox factories for clean, type-safe messaging.
 * 
 * @tparam MessageDefs Message definitions using MessageDefinition<PayloadType, ...>
 * 
 * **Key Features:**
 * - Compile-time message type registration with automatic ID generation
 * - Zero-overhead type dispatch (all lookups resolved at compile time)
 * - Type-safe Module and Mailbox templates bound to this application
 * - Automatic collision detection for message IDs
 * 
 * **Usage Example:**
 * @code
 * // 1. Define your message types (plain POD structs)
 * struct SensorData {
 *     float temperature;
 *     uint32_t sensor_id;
 * };
 * 
 * struct FilteredData {
 *     float filtered_value;
 *     float confidence;
 * };
 * 
 * // 2. Define your application
 * using MyApp = CommRaT<
 *     MessageDefinition<SensorData>,
 *     MessageDefinition<FilteredData>
 * >;
 * 
 * // 3. Create modules using MyApp::Module
 * class SensorModule : public MyApp::Module<Output<SensorData>, PeriodicInput> {
 * protected:
 *     void process(SensorData& output) override {
 *         output.temperature = read_sensor();
 *         output.sensor_id = config_.instance_id;
 *     }
 * };
 * 
 * class FilterModule : public MyApp::Module<Output<FilteredData>, Input<SensorData>> {
 * protected:
 *     void process(const SensorData& input, FilteredData& output) override {
 *         output.filtered_value = apply_filter(input.temperature);
 *         output.confidence = 0.95f;
 *     }
 * };
 * @endcode
 * 
 * **Advantages over raw MessageRegistry:**
 * - Module template alias included (Registry::Module wasn't clear to users)
 * - Mailbox types available via MyApp::Mailbox<T>
 * - All application components in one namespace
 * - Clearer intent: "This is my CommRaT application definition"
 * - Automatic system message inclusion (subscription protocol, etc.)
 * 
 * @see Module for module creation
 * @see MessageDefinition for defining message types
 * @see Output, Input, PeriodicInput for I/O specifications
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
        
        // WorkMailbox for subscription protocol (WORK mailbox handles Subscribe/Unsubscribe)
        using WorkMailbox = TypedMailbox<
            Registry,
            SubscribeRequestPayload,
            SubscribeReplyPayload,
            UnsubscribeRequestPayload,
            UnsubscribeReplyPayload
        >;
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
     * @brief Module template - create modules for this application
     * 
     * This is the preferred user-facing API for defining modules.
     * Automatically binds the module to this application's message registry.
     * 
     * @tparam OutputSpec_ Output specification: Output<T>, Outputs<T, U, ...>, or NoOutput
     * @tparam InputSpec_ Input specification: Input<T>, Inputs<T, U, ...>, PeriodicInput, or LoopInput
     * @tparam CommandTypes Optional command types this module handles
     * 
     * **Usage:**
     * @code
     * class MyModule : public MyApp::Module<OutputSpec, InputSpec, ...Commands> {
     * protected:
     *     void process(...) override {
     *         // Your implementation - signature depends on I/O spec
     *     }
     * };
     * @endcode
     * 
     * **I/O Specification Examples:**
     * - `Module<Output<T>, PeriodicInput>` - Periodic generation of single output
     * - `Module<Output<T>, Input<U>>` - Transform U → T on each input
     * - `Module<Outputs<T, U>, PeriodicInput>` - Periodic generation of multiple outputs
     * - `Module<Output<T>, Inputs<U, V>>` - Fuse U and V → T with synchronization
     * - `Module<Output<T>, LoopInput>` - Maximum throughput computation
     * 
     * @see Output, Outputs for output specifications
     * @see Input, Inputs, PeriodicInput, LoopInput for input specifications
     * @see ModuleConfig for configuration structure
     */
    // TODO reactivate
    // template<typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
    // using Module = commrat::Module2<Registry, OutputSpec_, InputSpec_, CommandTypes...>;
    
    /**
     * @brief Mailbox template - create mailboxes for this application
     * 
     * For advanced use cases that need direct mailbox access outside of Module.
     * Most users won't need this - Module handles mailboxes automatically.
     * 
     * @tparam PayloadT Message payload type (must be registered in this application)
     * 
     * **Usage (Advanced):**
     * @code
     * MyApp::Mailbox<SensorData> sensor_mailbox{address};
     * 
     * // Send
     * SensorData data{.temperature = 25.5f};
     * sensor_mailbox.send(data, dest_address);
     * 
     * // Receive
     * TimsMessage<SensorData> msg;
     * if (sensor_mailbox.receive(msg, Milliseconds(100))) {
     *     process(msg.payload);
     * }
     * @endcode
     * 
     * @note Real-time safe if PayloadT serialization is real-time safe
     * @see HistoricalMailbox for timestamp-synchronized receives
     */
    template<typename PayloadT>
    using Mailbox = commrat::Mailbox<MessageDefs..., SubscribeRequest, UnsubscribeRequest>;
    
    /**
     * @brief Module2 template - create modules for this application using new I/O tuple architecture
     * 
     * Module2 uses variadic I/O specifications for cleaner API and better compilation.
     * 
     * @tparam IOSpecs... Variadic I/O specifications (Output<T>, Input<T>, Period<D>, SyncedInput<T>)
     * 
     * **Usage:**
     * @code
     * class MySensor : public MyApp::Module2<Output<SensorData>, Period<Milliseconds(10)>> {
     * protected:
     *     void process(SensorData& output) override {
     *         output.value = read_sensor();
     *     }
     * };
     * 
     * class MyFilter : public MyApp::Module2<Output<FilteredData>, Input<SensorData>> {
     * protected:
     *     void process(const SensorData& input, FilteredData& output) override {
     *         output.value = filter(input.value);
     *     }
     * };
     * @endcode
     * 
     * @note Module2 requires CommRaT (not raw MessageRegistry) for System::WorkMailbox
     */
    template<typename... IOSpecs>
    using Module2 = commrat::Module2<CommRaT, IOSpecs...>;
    
    /**
     * @brief HistoricalMailbox template - mailbox with time-synchronized get_data()
     * 
     * Provides RACK-style timestamp-based message retrieval for multi-rate sensor fusion.
     * Buffers recent messages and returns the closest match to a requested timestamp.
     * 
     * @tparam HistorySize Number of messages to buffer per type (circular buffer)
     * 
     * **Use Case:** Synchronizing sensors with different rates
     * @code
     * // IMU at 100Hz, GPS at 10Hz - need to fuse
     * MyApp::HistoricalMailbox<100> gps_history{gps_address, 50'000'000}; // 50ms tolerance
     * 
     * // In fusion module (driven by IMU)
     * void process(const IMUData& imu, FusedData& output) {
     *     // Get GPS data closest to IMU timestamp
     *     auto gps = gps_history.get_data<GPSData>(imu.header.timestamp);
     *     if (gps) {
     *         output = fuse(imu, *gps);
     *     }
     * }
     * @endcode
     * 
     * **Algorithm:**
     * 1. Maintains circular buffer of recent messages with timestamps
     * 2. `get_data(target_timestamp, tolerance)` finds closest message
     * 3. Returns std::nullopt if no message within tolerance
     * 
     * @note Memory: `sizeof(TimsMessage<T>) * HistorySize` per type
     * @note Thread-safe: Uses internal mutex for concurrent access
     * @see Inputs for automatic multi-input synchronization
     */
    //TODO Check about that.
    //template<std::size_t HistorySize>
    //using HistoricalMailbox = commrat::HistoricalMailbox<Registry, HistorySize>;
    
    /**
     * @brief Introspection helper - export message schemas to any format
     * 
     * Provides registry-wide schema export combining:
     * - CommRaT metadata (message IDs, type names, size bounds)
     * - SeRTial layout (fields, types, offsets, sizes, variable flags)
     * 
     * Supports any rfl format: JSON, YAML, TOML, XML, etc.
     * 
     * **Usage:**
     * @code
     * // Export single message schema to JSON
     * auto json = MyApp::Introspection::export_as<TempData, rfl::json>();
     * 
     * // Export all message schemas to YAML
     * auto yaml = MyApp::Introspection::export_all<rfl::yaml>();
     * 
     * // Write all schemas to file
     * MyApp::Introspection::write_to_file<rfl::json>("schemas.json");
     * @endcode
     * 
     * **What's exported:**
     * - `message_id` - Unique ID for routing/filtering
     * - `payload_type` - Human-readable type name
     * - `full_type` - Complete TimsMessage<T> type
     * - `max_message_size` - Buffer allocation hint
     * - Field-level metadata (names, types, offsets, sizes)
     * 
     * **Use cases:**
     * - Logger tools: Record message structure with data
     * - Viewer tools: Display message fields dynamically
     * - Documentation: Auto-generate API docs
     * - Debugging: Understand message layout at runtime
     * 
     * @see IntrospectionHelper for detailed API
     * @see MessageSchema for schema structure
     */
    using Introspection = IntrospectionHelper<CommRaT>;
};

} // namespace commrat
