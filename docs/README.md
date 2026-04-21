# CommRaT Documentation

**Last Updated**: April 13, 2026

---

## Quick Links

- **[Getting Started](GETTING_STARTED.md)** - Installation and first program
- **[User Guide](USER_GUIDE.md)** - Comprehensive guide
- **[API Reference](API_REFERENCE.md)** - Complete API documentation
- **[Architecture](ARCHITECTURE.md)** - Design and internals
- **[Known Issues](KNOWN_ISSUES.md)** - Active issues and limitations
- **[Roadmap](ROADMAP.md)** - Planned features
- **[Internal Documentation](internal/)** - Design decisions and development history

---

## Current Features

### Core Messaging
- `CommRaT<MessageDefs...>` application template
- Compile-time message IDs (0xPSMM: Prefix, SubPrefix, MessageID)
- Zero-allocation serialization via SeRTial
- Request/reply protocol with auto-generated reply message types
- System messages auto-included (Subscribe, Unsubscribe, GetData, GetNextData)

### Module2 Framework
- `Module2<Output<T>, Input<T>, Period<D>, SyncedInput<T>>` I/O tuple architecture
- Zero-copy workspace API: write directly to output buffer, publish when ready
- Execution modes: timer-driven (`Period<D>`), input-driven (`Input<T>`), loop (no input spec)
- Multi-output: separate `Output<T>` per type, each with independent CMD mailbox
- Multi-input: `Input<T>` (primary, continuous) + `SyncedInput<T>` (secondary, time-synced)
- `Synced<T>` wrapper with explicit fresh/stale/invalid handling
- Lifecycle hooks: `on_init()`, `on_start()`, `on_stop()`, `on_cleanup()`

### 3-Mailbox Architecture
```
CMD  mailbox: per-output  - Receive commands/subscription requests (blocking receive thread)
WORK mailbox: per-module  - Send-only for outbound messages (no thread)
DATA mailbox: per-input   - Receive continuous data streams (blocking receive)
```

### Introspection
- `MessageSchema<T, Registry>` combining CommRaT + SeRTial metadata
- Export as JSON, YAML, TOML, XML via rfl Writer types
- `IntrospectionHelper<Registry>` for registry-wide schema export

### Platform Abstraction
- `Duration` structural type with constexpr arithmetic (NTTP-compatible)
- `Thread`, `Mutex`, `SharedMutex`, `ConditionVariable` wrappers
- `Time::now()`, `Time::sleep()` with platform-selected backend
- Compile-time backend selection via `COMMRAT_PLATFORM=STD` (default) or `COMMRAT_PLATFORM=EVL` CMake option

---

## Application Definition

```cpp
using MyApp = commrat::CommRaT<
    commrat::Message::Data<SensorData>,
    commrat::Message::Data<FilteredData>,
    commrat::Message::Command<ResetCmd>
>;
```

`MyApp` provides: `Module2<IOSpecs...>`, `get_message_id<T>()`, `serialize()`, `deserialize<T>()`, `size()`.

## Module Patterns

**Timer-driven producer:**
```cpp
class Sensor : public MyApp::Module2<Output<SensorData>, Period<Milliseconds(100)>> {
    void process(SensorData& output) override {
        output = read_sensor();
    }
};
```

**Input-driven consumer:**
```cpp
class Filter : public MyApp::Module2<Output<FilteredData>, Input<SensorData>> {
    void process(const SensorData& input, FilteredData& output) override {
        output = apply_filter(input);
    }
};
```

**Multi-output:**
```cpp
class MultiSensor : public MyApp::Module2<Output<DataA>, Output<DataB>, Period<Milliseconds(50)>> {
    void process(DataA& a, DataB& b) override {
        a = read_a(); b = read_b();
    }
};
```

**Multi-input with synchronization:**
```cpp
class Fusion : public MyApp::Module2<
    Output<FusedData>, Input<IMUData>, SyncedInput<GPSData>
> {
    void process(const IMUData& imu, Synced<GPSData> gps, FusedData& output) override {
        if (gps) output = fuse(imu, gps.value());
        else     output = dead_reckoning(imu);
    }
};
```

---

## Design Principles

1. **Compile-time everything**: Message IDs, type safety, buffer sizes computed at compile time
2. **Zero-copy where possible**: Direct memory access, workspace pattern, reference-based APIs
3. **Real-time safe**: No dynamic allocation in hot paths, blocking receives (0% CPU when idle)
4. **Simple user API**: Users see message types and Module2 -- metaprogramming hidden

---

## Documentation Structure

```
docs/
  README.md              -- This file (overview + quick reference)
  GETTING_STARTED.md     -- First-time tutorial
  USER_GUIDE.md          -- Comprehensive guide (sections 1-8)
  API_REFERENCE.md       -- API documentation
  ARCHITECTURE.md        -- Internal architecture
  KNOWN_ISSUES.md        -- Active bugs and limitations
  ROADMAP.md             -- Planned features
  work/                  -- Active design documents
  internal/              -- Design decisions, phase history
  archive/               -- Superseded documents
  examples/              -- Tutorial walkthroughs (01-03)
```
