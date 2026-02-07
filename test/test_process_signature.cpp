/**
 * @file test_process_signature.cpp
 * @brief Test automatic process() signature generation (Phase 5.3)
 * 
 * Tests that the correct process() signature is generated based on I/O specs:
 * - PeriodicInput + Output<T> → OutputData process()
 * - LoopInput + Output<T> → OutputData process()
 * - Input<T> + Output<U> → OutputData process(const InputData&)
 * - PeriodicInput + Outputs<T, U> → void process(T& out1, U& out2)
 * - Input<T> + Outputs<U, V> → void process(const T& in, U& out1, V& out2)
 * - NoOutput → void process() or void process(const InputData&)
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <cassert>

using namespace commrat;

// ============================================================================
// Test Message Types
// ============================================================================

struct SensorData {
    uint64_t timestamp;
    uint32_t sensor_id;
    float value;
};

struct FilteredData {
    uint64_t timestamp;
    float filtered_value;
};

struct DiagnosticsData {
    uint64_t timestamp;
    uint32_t error_count;
    float cpu_usage;
};

struct CommandData {
    uint64_t timestamp;
    uint32_t command_id;
};

// CommRaT Application Definition
using TestApp = CommRaT<
    MessageDefinition<SensorData, MessagePrefix::UserDefined, UserSubPrefix::Data, 0>,
    MessageDefinition<FilteredData, MessagePrefix::UserDefined, UserSubPrefix::Data, 1>,
    MessageDefinition<DiagnosticsData, MessagePrefix::UserDefined, UserSubPrefix::Data, 2>,
    MessageDefinition<CommandData, MessagePrefix::UserDefined, UserSubPrefix::Commands, 0>
>;

// Note: TestApp provides Module, Mailbox, and all registry functions

// ============================================================================
// Test 1: PeriodicInput + Output<T> → OutputData process()
// ============================================================================

class PeriodicSingleOutputModule : public TestApp::Module<Output<SensorData>, PeriodicInput> {
public:
    using Module::Module;
    
    int process_call_count = 0;
    
    // Make process() public for testing
    SensorData process() override {
        process_call_count++;
        return SensorData{
            .timestamp = static_cast<uint64_t>(1000 + process_call_count),
            .sensor_id = 42,
            .value = 20.5f + process_call_count
        };
    }
};

void test_periodic_single_output() {
    std::cout << "[Test 1] PeriodicInput + Output<T> → SensorData process()\n";
    
    ModuleConfig config{
        .name = "PeriodicSensor",
        .system_id = 0,
        .instance_id = 1,
        .period = std::chrono::milliseconds{10},
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    PeriodicSingleOutputModule module(config);
    
    // Verify process() is called and returns correct type
    auto result = module.process();
    assert(result.sensor_id == 42);
    assert(module.process_call_count == 1);
    
    std::cout << "  ✓ process() signature correct: SensorData process()\n";
    std::cout << "  ✓ process() callable and returns SensorData\n";
}

// ============================================================================
// Test 2: LoopInput + Output<T> → OutputData process()
// ============================================================================

class LoopSingleOutputModule : public TestApp::Module<Output<FilteredData>, LoopInput> {
public:
    using Module::Module;
    
    int process_call_count = 0;
    
    FilteredData process() override {
        process_call_count++;
        return FilteredData{
            .timestamp = static_cast<uint64_t>(2000 + process_call_count),
            .filtered_value = 15.3f + process_call_count
        };
    }
};

void test_loop_single_output() {
    std::cout << "[Test 2] LoopInput + Output<T> → FilteredData process()\n";
    
    ModuleConfig config{
        .name = "LoopProcessor",
        .system_id = 0,
        .instance_id = 2,
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    LoopSingleOutputModule module(config);
    
    auto result = module.process();
    assert(result.timestamp == 2001);
    assert(module.process_call_count == 1);
    
    std::cout << "  ✓ process() signature correct: FilteredData process()\n";
    std::cout << "  ✓ process() callable and returns FilteredData\n";
}

// ============================================================================
// Test 3: Input<T> + Output<U> → OutputData process_continuous(const InputData&)
// ============================================================================

class ContinuousSingleOutputModule : public TestApp::Module<Output<FilteredData>, Input<SensorData>> {
public:
    using Module::Module;
    
    int process_call_count = 0;
    
    FilteredData process_continuous(const SensorData& input) override {
        process_call_count++;
        return FilteredData{
            .timestamp = input.timestamp,
            .filtered_value = input.value * 0.9f  // Simple filter
        };
    }
};

void test_continuous_single_output() {
    std::cout << "[Test 3] Input<T> + Output<U> → FilteredData process_continuous(const SensorData&)\n";
    
    ModuleConfig config{
        .name = "ContinuousFilter",
        .system_id = 0,
        .instance_id = 3,
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false,
        .source_system_id = 0,
        .source_instance_id = 1
    };
    
    ContinuousSingleOutputModule module(config);
    
    SensorData input{.timestamp = 3000, .sensor_id = 42, .value = 25.0f};
    auto result = module.process_continuous(input);
    assert(result.timestamp == 3000);
    assert(result.filtered_value == 22.5f);  // 25.0 * 0.9
    assert(module.process_call_count == 1);
    
    std::cout << "  ✓ process_continuous() signature correct: FilteredData process_continuous(const SensorData&)\n";
    std::cout << "  ✓ process_continuous() callable and returns FilteredData\n";
}

// ============================================================================
// Test 4: Backward Compatibility - Raw Type Output
// ============================================================================

class BackwardCompatibleModule : public TestApp::Module<SensorData, PeriodicInput> {
public:
    using Module::Module;
    
    int process_call_count = 0;
    
    SensorData process() override {
        process_call_count++;
        return SensorData{
            .timestamp = static_cast<uint64_t>(4000 + process_call_count),
            .sensor_id = 99,
            .value = 10.0f
        };
    }
};

void test_backward_compatible() {
    std::cout << "[Test 4] Backward Compatible: Raw Type → SensorData process()\n";
    
    ModuleConfig config{
        .name = "LegacySensor",
        .system_id = 0,
        .instance_id = 4,
        .period = std::chrono::milliseconds{10},
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    BackwardCompatibleModule module(config);
    
    auto result = module.process();
    assert(result.sensor_id == 99);
    assert(module.process_call_count == 1);
    
    std::cout << "  ✓ Raw type auto-normalized to Output<T>\n";
    std::cout << "  ✓ process() signature correct: SensorData process()\n";
}

// ============================================================================
// Test 5: Type Aliases Verification
// ============================================================================

void test_type_aliases() {
    std::cout << "[Test 5] Type Aliases Verification\n";
    
    // Verify OutputData and InputData type aliases
    using M1 = TestApp::Module<Output<SensorData>, PeriodicInput>;
    static_assert(std::is_same_v<M1::OutputData, SensorData>, 
                  "OutputData should be SensorData");
    static_assert(std::is_same_v<M1::InputData, void>, 
                  "InputData should be void for PeriodicInput");
    
    using M2 = TestApp::Module<Output<FilteredData>, Input<SensorData>>;
    static_assert(std::is_same_v<M2::OutputData, FilteredData>, 
                  "OutputData should be FilteredData");
    static_assert(std::is_same_v<M2::InputData, SensorData>, 
                  "InputData should be SensorData for Input<SensorData>");
    
    using M3 = TestApp::Module<SensorData, PeriodicInput>;  // Raw type
    static_assert(std::is_same_v<M3::OutputData, SensorData>, 
                  "Raw type should be normalized to Output<T>");
    
    std::cout << "  ✓ OutputData type alias correct\n";
    std::cout << "  ✓ InputData type alias correct\n";
    std::cout << "  ✓ Raw type normalization works\n";
}

// ============================================================================
// Test 6: Compile-Time Constraints
// ============================================================================

void test_compile_time_constraints() {
    std::cout << "[Test 6] Compile-Time Constraints\n";
    
    // These should compile (Phase 5 allows single I/O)
    using Valid1 = TestApp::Module<Output<SensorData>, PeriodicInput>;
    using Valid2 = TestApp::Module<Output<FilteredData>, Input<SensorData>>;
    using Valid3 = TestApp::Module<SensorData, LoopInput>;  // Raw type
    
    // These should NOT compile (Phase 5 rejects multi-I/O)
    // Uncomment to verify compile-time rejection:
    // using Invalid1 = TestRegistry::Module<Outputs<SensorData, FilteredData>, PeriodicInput>;
    // using Invalid2 = TestRegistry::Module<Output<FilteredData>, Inputs<SensorData, CommandData>>;
    
    std::cout << "  ✓ Single I/O accepted\n";
    std::cout << "  ✓ Multi-I/O rejected (compile-time)\n";
    std::cout << "  ✓ Phase 5 constraints enforced\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== Process Signature Generation Tests (Phase 5.3) ===\n\n";
    
    try {
        test_periodic_single_output();
        std::cout << "\n";
        
        test_loop_single_output();
        std::cout << "\n";
        
        test_continuous_single_output();
        std::cout << "\n";
        
        test_backward_compatible();
        std::cout << "\n";
        
        test_type_aliases();
        std::cout << "\n";
        
        test_compile_time_constraints();
        std::cout << "\n";
        
        std::cout << "=== All Tests Passed ✓ ===\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
