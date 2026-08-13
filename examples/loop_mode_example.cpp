/**
 * @file loop_mode_example.cpp
 * @brief Demonstrates loop-driven mode for blocking-I/O style modules
 *
 * Loop-driven mode runs process() in a tight loop without any built-in sleep.
 * The intended use case is modules whose process() call naturally blocks, e.g.:
 *
 *   - Device drivers: blocking read() / ioctl() on a character device
 *   - Serial / CAN bus receivers: blocking receive call
 *   - Simulation tick loops with their own synchronisation primitive
 *
 * CommRaT inserts a yield() between iterations so other threads (including the
 * main thread that calls stop()) always get CPU time.  If process() itself
 * blocks, the yield() is effectively free.
 *
 * WARNING: If process() returns immediately without blocking, this mode will
 * consume 100% of one CPU core (modulo the yield).  Use a Period<> module
 * instead for pure computational / timer-driven work.
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace commrat;

// ============================================================================
// Message Definition
// ============================================================================

struct CounterData {
    uint64_t value;
    uint64_t iterations_per_second;
};

// CommRaT Application
using CounterApp = CommRaT<Message::Data<CounterData>>;

// ============================================================================
// Fast Counter Module (LoopInput)
// ============================================================================

/**
 * Counter module runs as fast as possible without sleeping.
 * Demonstrates LoopInput mode for maximum throughput.
 * 
 * Key characteristics:
 * - No sleep between iterations
 * - Runs on dedicated thread
 * - 100% CPU usage on one core
 * - Measures actual throughput
 */
class FastCounterModule : public CounterApp::Module2<Output<CounterData>> {
public:
    explicit FastCounterModule(const ModuleConfig& config)
        : Module2(config)
        , value_(0)
        , iteration_count_(0)
        , last_report_time_(Time::now())
    {}

protected:
    void process(CounterData& output) override {
        // auto data = recv();  // If this were a blocking I/O module, we'd call recv() here.
        Time::sleep(Microseconds(100));  // Simulate a blocking call (e.g., read() from a device)
                                       // relax scheduling and allow other threads to run.

        output.value = value_++;
        
        // Calculate throughput every second using OOB-safe Time::now()
        ++iteration_count_;
        Timestamp now = Time::now();
        Duration elapsed = Duration::nanoseconds(static_cast<int64_t>(now - last_report_time_));
        if (elapsed >= Seconds(1)) {
            output.iterations_per_second = iteration_count_;
            RTLOG_INFO(logger_) << "[FastCounter] count=" << value_ << " throughput=" << output.iterations_per_second << " iter/s";
            iteration_count_ = 0;
            last_report_time_ = now;
        } else {
            output.iterations_per_second = 0;
        }
    }

private:
    uint64_t value_;
    uint64_t iteration_count_;
    Timestamp last_report_time_;
};

// ============================================================================
// Throughput Monitor Module (ContinuousInput)
// ============================================================================

/**
 * Monitors the counter module and reports statistics.
 * Uses Input<CounterData> to process every message from the counter.
 */
class ThroughputMonitor : public CounterApp::Module2<Output<CounterData>, Input<CounterData>> {
public:
    explicit ThroughputMonitor(const ModuleConfig& config)
        : Module2(config)
        , total_messages_(0)
        , min_throughput_(UINT64_MAX)
        , max_throughput_(0)
        , sum_throughput_(0)
        , count_throughput_(0)
    {}

protected:
    void process(const CounterData& input, CounterData& output) override {
        ++total_messages_;
        
        // Track throughput statistics (only when report is available)
        if (input.iterations_per_second > 0) {
            if (input.iterations_per_second < min_throughput_) {
                min_throughput_ = input.iterations_per_second;
            }
            if (input.iterations_per_second > max_throughput_) {
                max_throughput_ = input.iterations_per_second;
            }
            sum_throughput_ += input.iterations_per_second;
            ++count_throughput_;
            
            RTLOG_INFO(logger_) << "[Monitor] msgs=" << total_messages_ << " throughput=" << input.iterations_per_second << " iter/s";
        }
        
        // Return pass-through data
        output = input;
    }
    
    void on_stop() override {
        RTLOG_INFO(logger_) << "[Monitor] final total=" << total_messages_ << " msgs";
        if (count_throughput_ > 0) {
            RTLOG_INFO(logger_) << "[Monitor] min=" << min_throughput_ << " max=" << max_throughput_
                               << " avg=" << (sum_throughput_ / count_throughput_) << " iter/s";
        }
    }

private:
    uint64_t total_messages_;
    uint64_t min_throughput_;
    uint64_t max_throughput_;
    uint64_t sum_throughput_;
    uint64_t count_throughput_;
};

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "CommRaT LoopInput Mode Example\n";
    std::cout << "==============================\n\n";
    std::cout << "This example demonstrates maximum throughput processing.\n";
    std::cout << "The counter module will consume 100% of one CPU core.\n\n";
    
    try {
        // Counter module configuration (LoopInput - runs as fast as possible)
        ModuleConfig counter_config{
            .name = "FastCounter",
            .outputs = commrat::SimpleOutputConfig{.system_id = 0, .instance_id = 0},
            .inputs = commrat::NoInputConfig{},
            .period = std::chrono::milliseconds(0),  // Ignored for LoopInput
            .message_slots = 100,
            .max_subscribers = 8,
            .priority = 10,
            .realtime = false
        };
        
        // Monitor module configuration (subscribes to counter)
        ModuleConfig monitor_config{
            .name = "ThroughputMonitor",
            .outputs = commrat::SimpleOutputConfig{.system_id = 0, .instance_id = 1},
            .inputs = commrat::SingleInputConfig{.source_system_id = 0, .source_instance_id = 0},
            .period = std::chrono::milliseconds(0),
            .message_slots = 100,
            .max_subscribers = 8,
            .priority = 5,
            .realtime = false
        };
        
        std::cout << "Creating modules...\n";
        FastCounterModule counter(counter_config);
        ThroughputMonitor monitor(monitor_config);
        
        std::cout << "Starting modules...\n";
        counter.start();
        monitor.start();
        
        std::cout << "\nModules running for 10 seconds...\n";
        std::cout << "Watch the throughput numbers - they show iterations/sec.\n";
        std::cout << "Typical performance: 1-10 million iterations/sec depending on CPU.\n\n";
        
        Time::sleep(Seconds(10));
        
        std::cout << "\nStopping modules...\n";
        counter.stop();
        monitor.stop();
        
        std::cout << "\nExample completed successfully!\n\n";
        std::cout << "Key Points:\n";
        std::cout << "  - LoopInput runs without sleep between iterations\n";
        std::cout << "  - Achieves maximum throughput (millions of iterations/sec)\n";
        std::cout << "  - Uses 100% of one CPU core - use wisely!\n";
        std::cout << "  - Ideal for CPU-bound processing, not I/O-bound tasks\n";
        std::cout << "  - Can publish to subscribers just like PeriodicInput\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
