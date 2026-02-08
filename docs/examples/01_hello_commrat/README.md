# Example 01: Hello CommRaT

**Goal:** Create the simplest possible CommRaT system in 5 minutes.

**What you'll learn:**
- Define a message type
- Create a periodic producer module
- Create a continuous consumer module
- Configure and run both modules

**System diagram:**
```
CounterModule (100Hz) → DisplayModule
    Generates             Displays
    counter values        each value
```

## Prerequisites

- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.20+
- CommRaT library installed
- TiMS library installed

## Files

- `hello_commrat.cpp` - Complete example source
- `CMakeLists.txt` - Build configuration
- `README.md` - This file

## Quick Start

```bash
# Build
mkdir build && cd build
cmake ..
make

# Run
./hello_commrat
```

## Expected Output

```
=== Hello CommRaT ===

[Counter] Starting counter at 0
[Display] Ready to receive counter values

Running for 3 seconds...

[Counter] Generated: count=0
[Display] Received: count=0
[Counter] Generated: count=1
[Display] Received: count=1
[Counter] Generated: count=2
[Display] Received: count=2
[Counter] Generated: count=3
[Display] Received: count=3
...
[Counter] Generated: count=29
[Display] Received: count=29

Shutting down...
[Counter] Stopped at count=30
[Display] Received 30 messages total
Done!
```

## Code Walkthrough

### Message Definition

```cpp
struct CounterMessage {
    uint64_t timestamp;    // When this was generated
    uint32_t count;        // Counter value
};
```

Simple POD struct with two fields. No pointers, no dynamic allocation.

### Application Registration

```cpp
using HelloApp = CommRaT<
    Message::Data<CounterMessage>
>;
```

Register our message type. CommRaT generates:
- Unique message ID
- Serialization code
- Type validation

### Producer Module

```cpp
class CounterModule : public HelloApp::Module<
    Output<CounterMessage>,  // Produces CounterMessage
    PeriodicInput            // Runs periodically
> {
    CounterMessage process() override {
        return CounterMessage{
            .timestamp = Time::now(),
            .count = counter_++
        };
    }
};
```

Key points:
- Inherits from `HelloApp::Module<Output, Input>`
- Overrides `process()` (called every `config_.period`)
- Returns message - automatically published to subscribers
- Must use `override` keyword

### Consumer Module

```cpp
class DisplayModule : public HelloApp::Module<
    Output<void>,                 // No output (sink)
    Input<CounterMessage>         // Receives CounterMessage
> {
    void process_continuous(const CounterMessage& msg) override {
        std::cout << "[Display] Received: count=" << msg.count << "\n";
    }
};
```

Key points:
- `Output<void>` means this is a sink (no outputs)
- `Input<CounterMessage>` means it receives counter values
- Overrides `process_continuous(const CounterMessage&)`
- Called for each received message (event-driven)

### Configuration

```cpp
ModuleConfig counter_config{
    .name = "Counter",
    .system_id = 10,
    .instance_id = 1,
    .period = Milliseconds(100)  // Generate every 100ms (10Hz)
};

ModuleConfig display_config{
    .name = "Display",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,      // Subscribe to system 10 (Counter)
    .source_instance_id = 1      // Instance 1
};
```

Key points:
- Each module needs unique `system_id`
- Producer specifies `period` for PeriodicInput
- Consumer specifies `source_system_id` and `source_instance_id` for subscription

### Lifecycle

```cpp
CounterModule counter(counter_config);
DisplayModule display(display_config);

counter.start();   // Spawns threads, begins generating
display.start();   // Spawns threads, subscribes to counter

Time::sleep(Seconds(3));  // Let it run

counter.stop();   // Clean shutdown
display.stop();
```

## What Happens Under the Hood

1. **Initialization**: Modules create 3 mailboxes each (CMD, WORK, DATA)
2. **Start**: Threads spawned for command_loop, work_loop, data_thread
3. **Subscription**: Display sends SubscribeRequest to Counter's WORK mailbox
4. **Acknowledgment**: Counter adds Display to subscribers, sends SubscribeReply
5. **Data flow**: Every 100ms, Counter's process() generates message → published to Display's DATA mailbox
6. **Processing**: Display's process_continuous() receives and displays message
7. **Shutdown**: Threads exit cleanly when stop() called

## Common Mistakes

**Mistake 1: Wrong system IDs**
```cpp
// ERROR: Display subscribes to system 99, but Counter is system 10
display_config.source_system_id = 99;  // Won't receive messages!
```

**Mistake 2: Missing period**
```cpp
// ERROR: PeriodicInput requires period
counter_config.period = Duration(0);  // Invalid!
```

**Mistake 3: Forgetting override**
```cpp
// ERROR: Won't compile
CounterMessage process() {  // Missing override keyword
    return msg;
}
```

## Exercises

**Exercise 1:** Change the counter frequency to 1Hz (one message per second).

**Exercise 2:** Add a second consumer that logs to a file instead of stdout.

**Exercise 3:** Modify CounterMessage to include a string field (use `sertial::fixed_string<N>`).

**Exercise 4:** Add a filter module between Counter and Display that only passes even numbers.

## Next Steps

- **Example 02**: Multi-output modules (one producer, two data streams)
- **Example 03**: Multi-input modules (sensor fusion)
- **Example 04**: Command handling (control modules at runtime)

## Full Source Code

See `hello_commrat.cpp` for the complete implementation.
