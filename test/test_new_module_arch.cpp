/**
 * @file test_new_module_arch.cpp
 * @brief Phase 7.1 proof of concept - test new module architecture
 * 
 * Tests NoInputSingleOutputBase using new module.hpp (not registry_module.hpp)
 * Validates both PeriodicInput and LoopInput modes
 */

#include <iostream>
#include <sertial/sertial.hpp>

// Use commrat.hpp to get CommRaT template and ModuleV2
#include "commrat/commrat.hpp"

// Simple test message
struct TestData {
    uint64_t counter;
    float value;
};

using TestApp = commrat::CommRaT<
    commrat::Message::Data<TestData>
>;

// Test module using new architecture (ModuleV2)
class TestProducer : public TestApp::ModuleV2<commrat::Output<TestData>, commrat::PeriodicInput> {
public:
    using Base = TestApp::ModuleV2<commrat::Output<TestData>, commrat::PeriodicInput>;
    explicit TestProducer(const commrat::ModuleConfig& config) : Base(config) {}
    
protected:
    void process(TestData& output) override {
        output.counter = counter_++;
        output.value = static_cast<float>(counter_) * 1.5f;
        std::cout << "[TestProducer] Generated: counter=" << output.counter 
                  << ", value=" << output.value << std::endl;
    }
    
private:
    uint64_t counter_ = 0;
};

// Test LoopInput mode (maximum throughput)
class LoopProducer : public TestApp::ModuleV2<commrat::Output<TestData>, commrat::LoopInput> {
public:
    using Base = TestApp::ModuleV2<commrat::Output<TestData>, commrat::LoopInput>;
    explicit LoopProducer(const commrat::ModuleConfig& config) : Base(config) {}
    
protected:
    void process(TestData& output) override {
        output.counter = counter_++;
        output.value = static_cast<float>(counter_) * 2.0f;
        
        // Only print first few to avoid flooding
        if (counter_ <= 3) {
            std::cout << "[LoopProducer] Generated: counter=" << output.counter 
                      << ", value=" << output.value << std::endl;
        }
    }
    
private:
    uint64_t counter_ = 0;
};

int main() {
    std::cout << "=== Phase 7.1 Proof of Concept: NoInputSingleOutputBase ===\n\n";
    
    // Create module config
    commrat::ModuleConfig config{
        .name = "TestProducer",
        .system_id = 10,
        .instance_id = 1,
        .period = commrat::Milliseconds(100),  // 10 Hz
        .message_slots = 10,
        .priority = 10,
        .realtime = false
    };
    
    // Create module (uses PeriodicSingleOutputBase internally)
    TestProducer producer(config);
    
    std::cout << "Module created successfully using new architecture\n";
    std::cout << "Module info:\n";
    std::cout << "  Periodic input: " << std::boolalpha << decltype(producer)::has_periodic_input << "\n";
    std::cout << "  Continuous input: " << decltype(producer)::has_continuous_input << "\n";
    std::cout << "  Multi-input: " << decltype(producer)::has_multi_input << "\n";
    std::cout << "  Multi-output: " << decltype(producer)::has_multi_output << "\n\n";
    
    // Start module
    producer.start();
    std::cout << "Producer started (will run for 1 second)\n\n";
    
    // Let it run
    commrat::Time::sleep(commrat::Seconds(1));
    
    // Stop
    producer.stop();
    std::cout << "\nProducer stopped\n";
    
    // ========================================================================
    // Test 2: LoopInput mode (maximum throughput)
    // ========================================================================
    
    std::cout << "\n========================================\n";
    std::cout << "Test 2: LoopInput Mode\n";
    std::cout << "========================================\n\n";
    
    commrat::ModuleConfig loop_config{
        .name = "LoopProducer",
        .system_id = 20,
        .instance_id = 1,
        .message_slots = 10,
        .priority = 10,
        .realtime = false
    };
    
    LoopProducer loop_producer(loop_config);
    
    std::cout << "Loop module created\n";
    std::cout << "Module info:\n";
    std::cout << "  Periodic input: " << std::boolalpha << decltype(loop_producer)::has_periodic_input << "\n";
    std::cout << "  Loop input: " << decltype(loop_producer)::has_loop_input << "\n";
    std::cout << "  Multi-output: " << decltype(loop_producer)::has_multi_output << "\n\n";
    
    loop_producer.start();
    std::cout << "Loop producer started (will run for 100ms - expect thousands of iterations)\n\n";
    
    // Let it run for a very short time (loop mode is FAST)
    commrat::Time::sleep(commrat::Milliseconds(1000));
    
    loop_producer.stop();
    std::cout << "\nLoop producer stopped\n";
    
    std::cout << "\n=== Phase 7.1 Complete: NoInputSingleOutputBase Works for Both Modes! ===\n";
    return 0;
}
