/**
 * @file loop_mode_example.cpp
 * @brief Demonstrates LoopInput mode for maximum throughput processing
 * 
 * LoopInput runs as fast as possible without sleeping between iterations.
 * Use this mode for:
 * - Maximum throughput data processing
 * - CPU-bound computational tasks
 * - Counter/accumulator modules
 * - High-frequency signal generation
 * 
 * WARNING: LoopInput will consume 100% of one CPU core. Use wisely!
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
class FastCounterModule : public CounterApp::Module<Output<CounterData>, LoopInput> {
public:
    explicit FastCounterModule(const ModuleConfig& config)
        : Module(config)
        , value_(0)
        , iteration_count_(0)
        , last_report_time_(std::chrono::steady_clock::now())
    {}

protected:
    void process(CounterData& output) override {
        output.value = value_++;
        
        // Calculate throughput every second
        ++iteration_count_;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_report_time_
        ).count();
        
        if (elapsed >= 1) {
            output.iterations_per_second = iteration_count_;
            
            // Print throughput report (using fprintf for thread-safe output)
            fprintf(stdout, "[FastCounter] Count: %12lu | Throughput: %10lu iterations/sec\n", 
                    value_, iteration_count_);
            fflush(stdout);
            
            // Reset counters
            iteration_count_ = 0;
            last_report_time_ = now;
        } else {
            output.iterations_per_second = 0;  // Not ready yet
        }
    }

private:
    uint64_t get_timestamp() const {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
    }
    
    uint64_t value_;
    uint64_t iteration_count_;
    std::chrono::steady_clock::time_point last_report_time_;
};

// ============================================================================
// Throughput Monitor Module (ContinuousInput)
// ============================================================================

/**
 * Monitors the counter module and reports statistics.
 * Uses Input<CounterData> to process every message from the counter.
 */
class ThroughputMonitor : public CounterApp::Module<Output<CounterData>, Input<CounterData>> {
public:
    explicit ThroughputMonitor(const ModuleConfig& config)
        : Module(config)
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
            
            // Print monitor update
            fprintf(stdout, "[ThroughputMonitor] Received %lu messages | Current throughput: %lu iter/sec\n",
                    total_messages_, input.iterations_per_second);
            fflush(stdout);
        }
        
        // Return pass-through data
        output = input;
    }
    
    void on_stop() override {
        fprintf(stdout, "\n[ThroughputMonitor] Final Statistics:\n");
        fprintf(stdout, "  Total messages received: %lu\n", total_messages_);
        if (count_throughput_ > 0) {
            fprintf(stdout, "  Min throughput: %lu iter/sec\n", min_throughput_);
            fprintf(stdout, "  Max throughput: %lu iter/sec\n", max_throughput_);
            fprintf(stdout, "  Avg throughput: %lu iter/sec\n", sum_throughput_ / count_throughput_);
        }
        fflush(stdout);
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
            .system_id = 0,
            .instance_id = 0,
            .period = std::chrono::milliseconds(0),  // Ignored for LoopInput
            .message_slots = 100,
            .max_subscribers = 8,
            .priority = 10,
            .realtime = false
        };
        
        // Monitor module configuration (subscribes to counter)
        ModuleConfig monitor_config{
            .name = "ThroughputMonitor",
            .system_id = 0,
            .instance_id = 1,
            .period = std::chrono::milliseconds(0),
            .message_slots = 100,
            .max_subscribers = 8,
            .priority = 5,
            .realtime = false,
            .source_system_id = 0,      // Subscribe to counter
            .source_instance_id = 0
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
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
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
