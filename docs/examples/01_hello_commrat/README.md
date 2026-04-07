# Example 01: Hello CommRaT

**Goal:** Create the simplest possible CommRaT system.

**What you'll learn:**
- Define a message type
- Create a timer-driven producer (Module2 with Period<>)
- Create an input-driven consumer (Module2 with Input<>)
- Configure and run both modules

**System diagram:**
```
CounterModule (10Hz) --> DisplayModule
    Generates               Displays
    counter values          each value
```

## Quick Start

```bash
mkdir build && cd build
cmake .. && make
./hello_commrat
```

## Code Walkthrough

### Message Definition

```cpp
struct CounterMessage {
    uint64_t timestamp;    // When this was generated
    uint32_t count;        // Counter value
};

using HelloApp = CommRaT<Message::Data<CounterMessage>>;
```

### Producer Module

```cpp
class CounterModule : public HelloApp::Module2<
    Output<CounterMessage>,  // Produces CounterMessage
    Period<100>              // Timer-driven (100ms = 10Hz)
> {
protected:
    void process(CounterMessage& output) override {
        output = CounterMessage{
            .timestamp = Time::now(),
            .count = counter_++
        };
    }
};
```

- `Period<100>` runs `process()` every 100ms
- Output is automatically published to subscribers

### Consumer Module

```cpp
class DisplayModule : public HelloApp::Module2<
    Output<CounterMessage>,       // Pass-through output
    Input<CounterMessage>         // Receives CounterMessage
> {
protected:
    void process(const CounterMessage& input, CounterMessage& output) override {
        std::cout << "[Display] count=" << input.count << "\n";
        output = input;
    }
};
```

- `Input<CounterMessage>` blocks until data arrives (event-driven)
- Called for each received message

### Configuration

```cpp
SimpleOutputConfig counter_config{
    .name = "Counter",
    .system_id = 10,
    .instance_id = 1
};

SimpleOutputConfig display_config{
    .name = "Display",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1
};
```

- Consumer specifies `source_system_id`/`source_instance_id` to subscribe

### Lifecycle

```cpp
CounterModule counter(counter_config);
DisplayModule display(display_config);

counter.start();   // Spawns threads, begins generating
display.start();   // Spawns threads, subscribes to counter
Time::sleep(Seconds(3));
counter.stop();
display.stop();
```

## What Happens Under the Hood

1. Modules create 3 mailboxes each (CMD, WORK, DATA)
2. Display sends SubscribeRequest to Counter's CMD mailbox
3. Counter adds Display to subscribers
4. Every 100ms, Counter publishes via WORK to Display's DATA mailbox
5. Display's process() receives and displays each message

## Next Steps

- **Example 02**: Multi-output modules
- **Example 03**: Multi-input sensor fusion
