/**
 * Example 01: Hello CommRaT
 * 
 * The simplest possible CommRaT system:
 * - CounterModule: Generates counter values at 10Hz (PeriodicInput)
 * - DisplayModule: Receives and displays counter values (ContinuousInput)
 * 
 * Demonstrates:
 * - Message definition (plain POD struct)
 * - Application registration (CommRaT<...>)
 * - Producer module (PeriodicInput, process(output))
 * - Consumer module (Input<T>, process(input, output))
 * - Configuration (system_id, period, source_system_id)
 * - Lifecycle (start, run, stop)
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <csignal>
#include <atomic>

// Signal handler for clean shutdown
std::atomic<bool> shutdown_requested{false};
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        shutdown_requested.store(true);
    }
}

//-----------------------------------------------------------------------------
// STEP 1: Define your message structure
//-----------------------------------------------------------------------------

/**
 * CounterMessage - Simple counter value with timestamp
 * 
 * Requirements:
 * - Plain POD type (no pointers, no virtual functions)
 * - Fixed-size or bounded (no std::string, use sertial::fixed_string)
 * - Serializable with SeRTial
 */
struct CounterMessage {
    uint64_t timestamp;    // When this message was generated (ns since epoch)
    uint32_t count;        // Counter value (0, 1, 2, 3, ...)
};

//-----------------------------------------------------------------------------
// STEP 2: Register message types with CommRaT
//-----------------------------------------------------------------------------

/**
 * HelloApp - Application definition
 * 
 * This registers all message types used in your application.
 * CommRaT validates uniqueness, assigns IDs, and generates serialization.
 */
using HelloApp = commrat::CommRaT<
    commrat::Message::Data<CounterMessage>  // Register CounterMessage as data type
>;

//-----------------------------------------------------------------------------
// STEP 3: Create a producer module
//-----------------------------------------------------------------------------

/**
 * CounterModule - Generates counter values periodically
 * 
 * Template parameters:
 * - Output<CounterMessage>: This module produces CounterMessage
 * - PeriodicInput: Runs at fixed intervals (config_.period)
 * 
 * Override process() to generate data.
 */
class CounterModule : public HelloApp::Module<
    commrat::Output<CounterMessage>,  // What this module produces
    commrat::PeriodicInput            // How it generates data (timer-driven)
> {
public:
    /**
     * Constructor
     * @param config Module configuration (system_id, period, etc.)
     */
    explicit CounterModule(const commrat::ModuleConfig& config)
        : Module(config)
        , counter_(0)
    {
        std::cout << "[Counter] Starting counter at " << counter_ << "\n";
    }

    /**
     * Destructor - called on shutdown
     */
    ~CounterModule() override {
        std::cout << "[Counter] Stopped at count=" << counter_ << "\n";
    }

protected:
    /**
     * process() - Called every config_.period
     * 
     * This function MUST:
     * - Return quickly (< period)
     * - Not allocate memory (real-time safe)
     * - Not block on I/O
     * - Use 'override' keyword (it's virtual)
     * 
     * @return Message to publish to all subscribers
     */
    void process(CounterMessage& output) override {
        CounterMessage msg{
            .timestamp = commrat::Time::now(),  // Current time in nanoseconds
            .count = counter_++                  // Increment counter
        };
        
        std::cout << "[Counter] Generated: count=" << msg.count << "\n";
        
        // Return value is automatically published to all subscribers
        output = msg;
    }

private:
    uint32_t counter_;  // Current counter value
};

//-----------------------------------------------------------------------------
// STEP 4: Create a consumer module
//-----------------------------------------------------------------------------

/**
 * DisplayModule - Receives and displays counter values
 * 
 * Template parameters:
 * - Output<CounterMessage>: Returns the same message (pass-through sink)
 * - Input<CounterMessage>: Receives CounterMessage (event-driven)
 * 
 * Override process() to handle incoming messages.
 */
class DisplayModule : public HelloApp::Module<
    commrat::Output<CounterMessage>,     // Pass-through output
    commrat::Input<CounterMessage>       // What this module receives
> {
public:
    /**
     * Constructor
     * @param config Module configuration (system_id, source_system_id, etc.)
     */
    explicit DisplayModule(const commrat::ModuleConfig& config)
        : Module(config)
        , message_count_(0)
    {
        std::cout << "[Display] Ready to receive counter values\n";
    }

    /**
     * Destructor - called on shutdown
     */
    ~DisplayModule() override {
        std::cout << "[Display] Received " << message_count_ << " messages total\n";
    }

protected:
    /**
     * process() - Called for EACH received message
     * 
     * This function MUST:
     * - Not allocate memory (real-time safe)
     * - Not block indefinitely
     * - Use 'override' keyword (it's virtual)
     * 
     * @param msg The received CounterMessage
     * @return The same message (pass-through for sink behavior)
     */
    void process(const CounterMessage& msg, CounterMessage& output) override {
        message_count_++;
        
        // Display the received counter value
        std::cout << "[Display] Received: count=" << msg.count << "\n";
        
        // Could add logic here:
        // - Log to file
        // - Check thresholds
        // - Accumulate statistics
        // - etc.
        
        output = msg;  // Pass through (not published since no subscribers)
    }

private:
    uint32_t message_count_;  // Total messages received
};

//-----------------------------------------------------------------------------
// STEP 5: Wire everything together
//-----------------------------------------------------------------------------

int main() {
    // Register signal handlers for clean shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "=== Hello CommRaT ===\n\n";
    
    //-------------------------------------------------------------------------
    // Configure the counter module (producer)
    //-------------------------------------------------------------------------
    commrat::ModuleConfig counter_config{
        .name = "Counter",                        // Human-readable name (for debugging)
        .system_id = 10,                          // Unique system ID (must be unique)
        .instance_id = 1,                         // Instance within system
        .period = commrat::Milliseconds(100)      // Generate message every 100ms (10Hz)
    };
    
    //-------------------------------------------------------------------------
    // Configure the display module (consumer)
    //-------------------------------------------------------------------------
    commrat::ModuleConfig display_config{
        .name = "Display",           // Human-readable name
        .system_id = 20,             // Different system ID (must differ from producer)
        .instance_id = 1,            // Instance within system
        .source_system_id = 10,      // Subscribe to Counter (system 10)
        .source_instance_id = 1      // Instance 1 of Counter
    };
    
    //-------------------------------------------------------------------------
    // Create module instances
    //-------------------------------------------------------------------------
    CounterModule counter(counter_config);
    DisplayModule display(display_config);
    
    //-------------------------------------------------------------------------
    // Start both modules
    //-------------------------------------------------------------------------
    // start() spawns threads:
    // - command_loop() thread (handles commands)
    // - work_loop() thread (handles subscription protocol)
    // - data_thread_ (PeriodicInput timer or ContinuousInput receiver)
    
    counter.start();  // Begins generating counter values
    
    // Give producer time to initialize before consumer subscribes
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    display.start();  // Subscribes to counter and begins receiving
    
    //-------------------------------------------------------------------------
    // Run until Ctrl+C or 30 messages (approximately 3 seconds)
    //-------------------------------------------------------------------------
    std::cout << "\nRunning... (Press Ctrl+C to stop)\n\n";
    
    int seconds = 0;
    while (!shutdown_requested.load() && seconds < 3) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        seconds++;
    }
    
    //-------------------------------------------------------------------------
    // Clean shutdown
    //-------------------------------------------------------------------------
    std::cout << "\nShutting down...\n";
    
    // stop() signals threads to exit and waits for them to join
    display.stop();
    counter.stop();
    
    std::cout << "Done!\n";
    return 0;
}

//-----------------------------------------------------------------------------
// Expected Output
//-----------------------------------------------------------------------------
/*
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
[Counter] Generated: count=4
[Display] Received: count=4
...
[Counter] Generated: count=29
[Display] Received: count=29

Shutting down...
[Counter] Stopped at count=30
[Display] Received 30 messages total
Done!
*/

//-----------------------------------------------------------------------------
// What Happens Under the Hood
//-----------------------------------------------------------------------------
/*
Timeline:

0ms: main() starts
     ├─ tims::init()
     ├─ Create CounterModule
     │  └─ 3 mailboxes created (CMD, WORK, DATA)
     └─ Create DisplayModule
        └─ 3 mailboxes created (CMD, WORK, DATA)

10ms: counter.start()
      ├─ Spawn command_loop() thread (blocks on CMD mailbox)
      ├─ Spawn work_loop() thread (blocks on WORK mailbox)
      └─ Spawn data_thread_ (timer fires every 100ms)

15ms: display.start()
      ├─ Spawn command_loop() thread
      ├─ Spawn work_loop() thread
      ├─ Spawn data_thread_ (continuous_loop, blocks on DATA mailbox)
      └─ Send SubscribeRequest to counter's WORK mailbox

20ms: Counter's work_loop receives SubscribeRequest
      ├─ Add display's DATA mailbox to subscribers_ list
      └─ Send SubscribeReply to display's WORK mailbox

25ms: Display's work_loop receives SubscribeReply
      └─ Subscription confirmed

100ms: Counter's timer fires
       ├─ Call process(output) → generates CounterMessage{count=0}
       ├─ User code generates CounterMessage{count=0}
       └─ Publish to all subscribers (display's DATA mailbox)

100ms: Display's continuous_loop receives message
       ├─ Deserialize CounterMessage
       ├─ Call process(msg, output)
       └─ User code displays "count=0"
       └─ Block on DATA mailbox again

200ms: Counter's timer fires again
       └─ Repeat...

3000ms: main() wakes up
        ├─ counter.stop() → threads exit
        └─ display.stop() → threads exit
        └─ tims::cleanup()
*/
