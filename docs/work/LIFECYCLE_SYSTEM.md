# System Lifecycle Commands

**Status**: Planned  
**Priority**: High  
**Created**: February 12, 2026

## Overview

Extend SystemMessages to provide standardized lifecycle management for all modules. Every module automatically responds to lifecycle commands (on, off, reset) without user code.

## Current State

Modules have manual start/stop:
```cpp
MyModule module(config);
module.start();  // User-initiated
// ...
module.stop();   // User-initiated
```

No standardized way to:
- Remotely control module lifecycle
- Coordinate multi-module startup/shutdown
- Implement graceful shutdown sequences
- Reset module state without restart

## Proposed Design

### Lifecycle State Machine

```
┌──────────┐  start()   ┌─────────┐
│   INIT   ├───────────→│ RUNNING │
└──────────┘            └────┬────┘
                             │
                  ┌──────────┼──────────┐
                  │          │          │
               reset()    stop()    on_error()
                  │          │          │
                  ↓          ↓          ↓
            ┌─────────┐ ┌──────────┐ ┌───────┐
            │ RUNNING │ │ STOPPED  │ │ ERROR │
            └─────────┘ └──────────┘ └───────┘
                             │          │
                          restart()  recover()
                             ↓          ↓
                        ┌─────────┐    │
                        │ RUNNING │←───┘
                        └─────────┘
```

### System Commands

Extend `SystemMessages` enum:

```cpp
enum class SystemMessages : uint32_t {
    // Existing
    SubscribeRequest = 0x00000001,
    SubscribeReply = 0x00000002,
    UnsubscribeRequest = 0x00000003,
    UnsubscribeReply = 0x00000004,
    
    // NEW: Lifecycle commands
    LifecycleOn = 0x00000010,
    LifecycleOff = 0x00000011,
    LifecycleReset = 0x00000012,
    LifecycleStatus = 0x00000013,
    LifecycleStateChanged = 0x00000014
};
```

### Command Payloads

```cpp
struct LifecycleOnCmd {
    uint32_t timeout_ms{5000};  // Max startup time
};

struct LifecycleOffCmd {
    bool force{false};  // Graceful vs immediate shutdown
    uint32_t timeout_ms{5000};
};

struct LifecycleResetCmd {
    bool clear_state{true};  // Reset internal state
    bool resubscribe{true};  // Re-establish subscriptions
};

struct LifecycleStatusCmd {
    // Query current state
};

struct LifecycleStatusReply {
    enum class State : uint8_t {
        INIT,
        RUNNING,
        STOPPED,
        ERROR
    };
    
    State state;
    uint64_t uptime_ns;
    uint64_t message_count;
    uint32_t error_code;
    char error_message[128];
};

struct LifecycleStateChangedEvent {
    LifecycleStatusReply::State old_state;
    LifecycleStatusReply::State new_state;
    uint64_t timestamp;
};
```

## Automatic Handling

### Module Base Integration

```cpp
class Module {
    // Internal lifecycle state
    std::atomic<LifecycleState> lifecycle_state_{LifecycleState::INIT};
    
    // Automatic command handler (not user-visible)
    void handle_lifecycle_command(const LifecycleCommand& cmd) {
        switch (cmd.type) {
            case LifecycleOn:
                transition_to_running();
                break;
            case LifecycleOff:
                transition_to_stopped();
                break;
            case LifecycleReset:
                reset_internal_state();
                break;
        }
        
        // Publish state change event
        publish_state_change();
    }
    
protected:
    // User hooks (optional overrides)
    virtual void on_lifecycle_start() {}
    virtual void on_lifecycle_stop() {}
    virtual void on_lifecycle_reset() {}
};
```

### User API

```cpp
class MyModule : public MyApp::Module<Output<Data>, Input<Sensor>> {
protected:
    // Optional: Custom initialization
    void on_lifecycle_start() override {
        std::cout << "Module starting\n";
        // Initialize hardware, load calibration, etc.
    }
    
    // Optional: Cleanup
    void on_lifecycle_stop() override {
        std::cout << "Module stopping\n";
        // Close files, release resources, etc.
    }
    
    // Optional: Reset state
    void on_lifecycle_reset() override {
        std::cout << "Module resetting\n";
        counter_ = 0;
        filter_.clear();
    }
    
private:
    size_t counter_{0};
    Filter filter_;
};
```

## Orchestration

### Supervisor Module

```cpp
class SystemSupervisor {
    std::vector<ModuleHandle> modules_;
    
public:
    void startup_sequence() {
        // Start modules in dependency order
        for (auto& module : modules_) {
            send_command(module, LifecycleOnCmd{});
            wait_for_state(module, LifecycleState::RUNNING);
        }
    }
    
    void shutdown_sequence() {
        // Stop in reverse order
        for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
            send_command(*it, LifecycleOffCmd{.force = false});
            wait_for_state(*it, LifecycleState::STOPPED, Seconds(5));
        }
    }
    
    void emergency_stop() {
        // Force stop all immediately
        for (auto& module : modules_) {
            send_command(module, LifecycleOffCmd{.force = true});
        }
    }
};
```

## Benefits

1. **Remote Control**: Modules can be started/stopped remotely via commands
2. **Coordinated Startup**: Orchestrator ensures dependency order
3. **Graceful Shutdown**: Modules can clean up before termination
4. **State Monitoring**: Query module health and status
5. **Automatic Handling**: No boilerplate - Module base does everything
6. **Custom Hooks**: Optional user overrides for special behavior

## Implementation Plan

**Phase 1**: Define lifecycle messages and state machine (1 week)
**Phase 2**: Integrate into Module base class (1 week)
**Phase 3**: Implement supervisor/orchestrator utilities (1 week)
**Phase 4**: Update examples and documentation (1 week)

**Total Estimated Effort**: 4 weeks

## Related Work

- System messages: `include/commrat/messages.hpp`
- Module base: `include/commrat/registry_module.hpp`
- Command handling improvements: `docs/internal/phase_history/ARCHITECTURE_ANALYSIS.md`
