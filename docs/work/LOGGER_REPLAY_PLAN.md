# Generic Logger/Replay System Implementation Plan

## Overview

Create a type-agnostic logging and replay system that leverages CommRaT's introspection and message registry. The logger records messages with full metadata (type ID, message ID, timestamp, source), and the replay module reconstructs the exact message flow.

**Key Insight**: CommRaT knows all message types, IDs, and mailbox addresses at compile time - use this for zero-configuration logging.

## Core Concept

**Log Format Structure** (binary or JSON):
```cpp
struct LogEntry {
    uint64_t timestamp;           // From TimsHeader
    uint32_t message_id;          // Computed message ID
    uint32_t source_system_id;    // Who sent it
    uint32_t source_instance_id;
    uint32_t sequence_number;     // From TimsHeader
    uint32_t payload_size;        // Bytes following
    // Followed by: serialized payload (SeRTial packed format)
};
```

**Binary Format**: Direct SeRTial packed messages (real-time safe, minimal overhead)
**JSON Format**: Human-readable with field names via introspection

## Architecture

### 1. Generic Logger Module

```cpp
template<typename AppType>
class GenericLogger : public AppType::template Module<NoOutput, LoopInput> {
public:
    GenericLogger(const ModuleConfig& config, const LoggerConfig& log_config)
        : Module(config)
        , log_config_(log_config)
        , log_file_(log_config.output_file, std::ios::binary)
    {
        // Open log file
        write_header();
    }

protected:
    void process() override {
        // Receive from DATA mailbox (any message type)
        auto result = data_mailbox_.receive_any_timeout(Milliseconds(10));
        
        if (result.has_value()) {
            // Write log entry with full metadata
            write_log_entry(result.value());
        }
    }

private:
    void write_header() {
        // Write schema information for replay validation
        // Note: timestamp_start will be filled when first message arrives
        LogFileHeader header{
            .magic = 0x434F4D4D,  // "COMM"
            .version = 1,
            .message_count = AppType::message_count,
            .timestamp_start = 0  // Will be set from first message's TimsHeader
        };
        
        header_offset_ = log_file_.tellp();
        log_file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
        
        // Write message type registry (for replay validation)
        write_message_registry();
    }
    
    void write_message_registry() {
        // Export all message schemas to log file header
        auto schemas = AppType::Introspection::template export_all<rfl::json>();
        uint32_t schema_size = schemas.size();
        log_file_.write(reinterpret_cast<const char*>(&schema_size), sizeof(schema_size));
        log_file_.write(schemas.data(), schema_size);
    }
    
    template<typename T>
    void write_log_entry(const ReceivedMessage<T>& received) {
        // Use timestamp from TimsHeader (single source of truth)
        if (!first_timestamp_) {
            first_timestamp_ = received.header.timestamp;
            update_header_timestamp(received.header.timestamp);
        }
        
        LogEntry entry{
            .timestamp = received.header.timestamp,  // From TimsHeader
            .message_id = received.header.message_id,
            .source_system_id = received.header.source_system_id,
            .source_instance_id = received.header.source_instance_id,
            .sequence_number = received.header.sequence_number,
            .payload_size = 0  // Will be filled after serialization
        };
        
        // Serialize payload using SeRTial
        auto serialized = sertial::Message<T>::serialize(received.message);
        entry.payload_size = serialized.size();
        
        // Write entry + payload atomically
        log_file_.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        log_file_.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
        
        if (log_config_.format == LogFormat::JSON) {
            write_json_entry(received);
        }
    }
    
    void update_header_timestamp(uint64_t timestamp) {
        // Go back and update timestamp_start in header
        auto current_pos = log_file_.tellp();
        log_file_.seekp(header_offset_ + offsetof(LogFileHeader, timestamp_start));
        log_file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
        log_file_.seekp(current_pos);  // Restore position
    }
    
    template<typename T>
    void write_json_entry(const ReceivedMessage<T>& received) {
        // Human-readable JSON for debugging
        auto json = rfl::json::write(rfl::Named<
            rfl::Field<"timestamp", uint64_t>,
            rfl::Field<"message_id", uint32_t>,
            rfl::Field<"source", std::tuple<uint32_t, uint32_t>>,
            rfl::Field<"payload", T>
        >{
            received.header.timestamp,
            received.header.message_id,
            {received.header.source_system_id, received.header.source_instance_id},
            received.message
        });
        
        json_file_ << json << "\n";
    }

    LoggerConfig log_config_;
    std::ofstream log_file_;
    std::ofstream json_file_;  // Optional parallel JSON log
    std::streampos header_offset_;  // Location of header in file
    std::optional<uint64_t> first_timestamp_;  // From first message's TimsHeader
};
```

**Logger Configuration**:
```cpp
struct LoggerConfig {
    std::string output_file;
    LogFormat format;  // BINARY, JSON, or BOTH
    std::vector<uint32_t> message_ids_filter;  // Empty = log all
    bool include_schemas;  // Write type schemas to header
};
```

### 2. Replay Module

```cpp
template<typename AppType>
class ReplayModule : public AppType::template Module<NoOutput, PeriodicInput> {
public:
    ReplayModule(const ModuleConfig& config, const ReplayConfig& replay_config)
        : Module(config)
        , replay_config_(replay_config)
        , log_file_(replay_config.input_file, std::ios::binary)
    {
        read_header();
        validate_registry();
    }

protected:
    void process() override {
        // Read next log entry
        LogEntry entry;
        log_file_.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        
        if (log_file_.eof()) {
            if (replay_config_.loop) {
                log_file_.seekg(data_start_offset_);  // Restart
            } else {
                stop();
                return;
            }
        }
        
        // Read payload
        std::vector<std::byte> payload(entry.payload_size);
        log_file_.read(reinterpret_cast<char*>(payload.data()), entry.payload_size);
        
        // Dispatch to correct publish based on message_id
        dispatch_by_message_id(entry, payload);
        
        // Handle timing
        handle_playback_timing(entry);
    }

private:
    void read_header() {
        LogFileHeader header;
        log_file_.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        if (header.magic != 0x434F4D4D) {
            throw std::runtime_error("Invalid log file format");
        }
        
        // Read schema registry
        uint32_t schema_size;
        log_file_.read(reinterpret_cast<char*>(&schema_size), sizeof(schema_size));
        
        std::string schemas(schema_size, '\0');
        log_file_.read(schemas.data(), schema_size);
        
        logged_schemas_ = schemas;
        data_start_offset_ = log_file_.tellg();
    }
    
    void validate_registry() {
        // Compare logged schemas with current registry
        auto current_schemas = AppType::Introspection::template export_all<rfl::json>();
        
        if (current_schemas != logged_schemas_ && replay_config_.strict_validation) {
            throw std::runtime_error("Message registry mismatch - log incompatible");
        }
    }
    
    void dispatch_by_message_id(const LogEntry& entry, std::span<std::byte> payload) {
        // Use compile-time message ID map to dispatch
        bool handled = false;
        
        AppType::for_each_message_type([&]<typename T>() {
            if (AppType::template get_message_id<T>() == entry.message_id && !handled) {
                // Deserialize and publish
                auto result = sertial::Message<T>::deserialize(payload);
                if (result.has_value()) {
                    // Publish to original destination
                    publish_as_source(result.value(), entry);
                    handled = true;
                }
            }
        });
        
        if (!handled) {
            // Unknown message type (registry changed)
            handle_unknown_message(entry);
        }
    }
    
    template<typename T>
    void publish_as_source(const T& message, const LogEntry& entry) {
        // Reconstruct original header
        TimsHeader header{
            .timestamp = entry.timestamp,
            .sequence_number = entry.sequence_number,
            .message_id = entry.message_id,
            .source_system_id = entry.source_system_id,
            .source_instance_id = entry.source_instance_id
        };
        
        // Publish to subscribers (replay module acts as proxy)
        // Note: Subscribers must be configured to listen to replay module's ID
        this->template publish<T>(message);
    }
    
    void handle_playback_timing(const LogEntry& entry) {
        if (!first_timestamp_) {
            first_timestamp_ = entry.timestamp;
            playback_start_ = Time::now();
        }
        
        // Calculate when this message should be sent relative to start
        uint64_t log_elapsed = entry.timestamp - first_timestamp_.value();
        uint64_t scaled_elapsed = log_elapsed / replay_config_.playback_speed;
        
        Timestamp target_time = playback_start_.value() + Nanoseconds(scaled_elapsed);
        Timestamp now = Time::now();
        
        if (target_time > now) {
            // Sleep until target time
            Time::sleep(target_time - now);
        }
    }

    ReplayConfig replay_config_;
    std::ifstream log_file_;
    std::string logged_schemas_;
    std::streampos data_start_offset_;
    std::optional<uint64_t> first_timestamp_;
    std::optional<Timestamp> playback_start_;
};
```

**Replay Configuration**:
```cpp
struct ReplayConfig {
    std::string input_file;
    double playback_speed;      // 1.0 = real-time, 2.0 = 2x speed
    bool loop;                  // Restart when reaching end
    bool strict_validation;     // Error if schemas don't match
    std::optional<uint64_t> start_timestamp;  // Skip to specific time
    std::optional<uint64_t> end_timestamp;    // Stop at specific time
};
```

## Integration with CommRaT Application Metadata

**Key Enhancement**: Use application-level metadata for automatic configuration:

```cpp
template<typename... MessageDefs>
struct CommRaT {
    // ... existing code ...
    
    // ========================================================================
    // APPLICATION METADATA (NEW)
    // ========================================================================
    
    /// Compile-time mapping: message type -> message ID
    template<typename T>
    static constexpr uint32_t get_message_id() {
        return UserRegistry::template message_id_v<T>;
    }
    
    /// Runtime iteration over all message types
    template<typename Visitor>
    static void for_each_message_type(Visitor&& visitor) {
        for_each_message_type_impl(
            typename UserRegistry::payload_types{},
            std::forward<Visitor>(visitor)
        );
    }
    
    /// Get input type IDs for an output type
    template<typename OutputType>
    static constexpr auto get_input_type_ids() {
        // Extract from Module definition if available
        // Or maintain separate mapping
        return std::array<uint32_t, 0>{};  // Placeholder
    }
    
    /// Message ID -> Type name mapping (for logging)
    static std::string_view get_type_name(uint32_t message_id) {
        std::string_view result = "Unknown";
        
        for_each_message_type([&]<typename T>() {
            if (get_message_id<T>() == message_id) {
                result = rfl::type_name_t<T>().str();
            }
        });
        
        return result;
    }
    
    /// Count of registered message types
    static constexpr size_t message_count = std::tuple_size_v<
        typename UserRegistry::payload_types
    >;
    
private:
    template<typename... PayloadTypes, typename Visitor>
    static void for_each_message_type_impl(
        std::tuple<PayloadTypes...>,
        Visitor&& visitor
    ) {
        (visitor.template operator()<PayloadTypes>(), ...);
    }
};
```

## Logger/Replay Factory Functions

**Convenience creators**:
```cpp
template<typename AppType>
struct CommRaT {
    // ... existing code ...
    
    /// Create a logger module instance
    static auto create_logger(
        const ModuleConfig& config,
        const LoggerConfig& log_config
    ) {
        return std::make_unique<GenericLogger<AppType>>(config, log_config);
    }
    
    /// Create a replay module instance
    static auto create_replay(
        const ModuleConfig& config,
        const ReplayConfig& replay_config
    ) {
        return std::make_unique<ReplayModule<AppType>>(config, replay_config);
    }
};
```

## Usage Example

```cpp
using MyApp = commrat::CommRaT<
    commrat::Message::Data<TempData>,
    commrat::Message::Data<StatusData>
>;

int main() {
    commrat::initialize_tims();
    
    // Create logger to record all messages
    ModuleConfig logger_module_config{
        .name = "DataLogger",
        .system_id = 100,
        .instance_id = 1,
        .period = Milliseconds(1)  // Fast receive loop
    };
    
    LoggerConfig logger_config{
        .output_file = "session.log",
        .format = LogFormat::BINARY,  // Or BOTH for binary + JSON
        .message_ids_filter = {},     // Empty = log all
        .include_schemas = true
    };
    
    auto logger = MyApp::create_logger(logger_module_config, logger_config);
    
    // Subscribe logger to all sources
    logger->subscribe_to({10, 1});  // TempSensor
    logger->subscribe_to({11, 1});  // StatusMonitor
    
    logger->start();
    
    // ... run for a while ...
    
    logger->stop();
    
    // Later: replay the logged data
    ModuleConfig replay_module_config{
        .name = "DataReplay",
        .system_id = 101,
        .instance_id = 1,
        .period = Milliseconds(1)
    };
    
    ReplayConfig replay_config{
        .input_file = "session.log",
        .playback_speed = 1.0,       // Real-time
        .loop = false,
        .strict_validation = true
    };
    
    auto replay = MyApp::create_replay(replay_module_config, replay_config);
    replay->start();
    
    // Consumers subscribe to replay module (101, 1) instead of original sources
}
```

## Implementation Checklist

### Phase 1: Core Logger (High Priority - 4-6 hours)
- [ ] Create `include/commrat/logger.hpp`
- [ ] Implement `LogEntry` structure and binary format
- [ ] Implement `GenericLogger` module template
- [ ] Add `LoggerConfig` with format options
- [ ] Implement binary logging with SeRTial packed format
- [ ] Implement JSON parallel logging (optional)
- [ ] Add schema export to log file header

### Phase 2: Replay Module (High Priority - 4-6 hours)
- [ ] Create `include/commrat/replay.hpp`
- [ ] Implement `ReplayModule` template
- [ ] Add log file header validation
- [ ] Implement message dispatch by message ID
- [ ] Add playback timing control (speed, loop)
- [ ] Handle registry mismatches gracefully

### Phase 3: Application Metadata (Medium Priority - 2-3 hours)
- [ ] Add `for_each_message_type()` to CommRaT<>
- [ ] Add `get_type_name()` for message ID -> name mapping
- [ ] Add `message_count` constant
- [ ] Add factory functions `create_logger()` and `create_replay()`

### Phase 4: Examples and Testing (High Priority - 3-4 hours)
- [ ] Create `examples/logger_replay_example.cpp`
- [ ] Test binary logging format
- [ ] Test JSON logging format
- [ ] Test replay with timing preservation
- [ ] Test schema validation
- [ ] Document log file format

### Phase 5: Documentation (Medium Priority - 2 hours)
- [ ] Add section to USER_GUIDE.md
- [ ] Document LogEntry binary format
- [ ] Document log file header structure
- [ ] Add examples for common use cases
- [ ] Update ROADMAP.md

## Benefits

1. **Zero Configuration**: Logger works with any message type automatically
2. **Type Safety**: Message IDs ensure correct deserialization during replay
3. **Real-Time Safe**: Binary format uses bounded allocations
4. **Debugging Friendly**: Optional JSON logging for human inspection
5. **Portable**: Log files include schema for validation
6. **Flexible Playback**: Speed control, looping, time filtering

## Dependencies

- ✅ Introspection (INTROSPECTION_INTEGRATION_PLAN.md)
- ✅ SeRTial serialization
- ✅ rfl JSON support
- ⏳ Application metadata extensions to CommRaT<>

## Timeline

- **Phase 1** (Logger): 4-6 hours
- **Phase 2** (Replay): 4-6 hours
- **Phase 3** (Metadata): 2-3 hours
- **Phase 4** (Examples): 3-4 hours
- **Phase 5** (Docs): 2 hours
- **Total**: ~15-21 hours (2-3 days of focused work)
