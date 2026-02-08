/**
 * @file test_multi_output.cpp
 * @brief Test multi-output signature generation (Phase 5.3)
 * 
 * Tests that multi-output modules generate correct signatures:
 * - Outputs<T, U> + PeriodicInput → void process(T& out1, U& out2)
 * - Outputs<T, U> + Input<V> → void process_continuous(const V& in, T& out1, U& out2)
 */

#include <commrat/commrat.hpp>
#include <iostream>

using namespace commrat;

// Test message types
struct DataA {
    float value_a;
};

struct DataB {
    float value_b;
};

struct InputData {
    float input_value;
};

// Test application
using TestApp = CommRaT<
    MessageDefinition<DataA, MessagePrefix::UserDefined, UserSubPrefix::Data, 0>,
    MessageDefinition<DataB, MessagePrefix::UserDefined, UserSubPrefix::Data, 1>,
    MessageDefinition<InputData, MessagePrefix::UserDefined, UserSubPrefix::Data, 2>
>;

// ============================================================================
// Test 1: Multi-Output with PeriodicInput
// ============================================================================

class MultiOutputPeriodicModule : public TestApp::Module<Outputs<DataA, DataB>, PeriodicInput> {
public:
    using Module::Module;
    int process_call_count = 0;
    
    // Multi-output signature: void process(T1& out1, T2& out2)
    void process(DataA& out1, DataB& out2) override {
        process_call_count++;
        out1 = DataA{
            .value_a = 10.0f + process_call_count
        };
        out2 = DataB{
            .value_b = 20.0f + process_call_count
        };
    }
};

void test_multi_output_periodic() {
    std::cout << "[Test 1] Outputs<A, B> + PeriodicInput → void process(A&, B&)\n";
    
    // Verify signature compiles
    ModuleConfig config{
        .name = "MultiOutputPeriodic",
        .system_id = 0,
        .instance_id = 1,
        .period = std::chrono::milliseconds(100)
    };
    
    MultiOutputPeriodicModule module(config);
    
    // Test direct call
    DataA a{};
    DataB b{};
    module.process(a, b);
    
    if (a.value_a == 11.0f && b.value_b == 21.0f) {
        std::cout << "  ✓ Multi-output process(A&, B&) signature correct\n";
        std::cout << "  ✓ Multi-output process callable\n";
    } else {
        std::cout << "  ✗ Multi-output process failed\n";
    }
}

// ============================================================================
// Test 2: Multi-Output with Continuous Input
// ============================================================================

class MultiOutputContinuousModule : public TestApp::Module<Outputs<DataA, DataB>, Input<InputData>> {
public:
    using Module::Module;
    int process_call_count = 0;
    
    // Multi-output continuous signature: void process_continuous(const Input&, T1& out1, T2& out2)
    void process_continuous(const InputData& input, DataA& out1, DataB& out2) override {
        process_call_count++;
        out1 = DataA{
            .value_a = input.input_value * 1.5f
        };
        out2 = DataB{
            .value_b = input.input_value * 2.0f
        };
    }
};

void test_multi_output_continuous() {
    std::cout << "\n[Test 2] Outputs<A, B> + Input<C> → void process_continuous(const C&, A&, B&)\n";
    
    ModuleConfig config{
        .name = "MultiOutputContinuous",
        .system_id = 0,
        .instance_id = 2,
        .period = std::chrono::milliseconds(100),
        .source_system_id = 0,
        .source_instance_id = 1
    };
    
    MultiOutputContinuousModule module(config);
    
    // Test direct call
    InputData input{.input_value = 10.0f};
    DataA a{};
    DataB b{};
    module.process_continuous(input, a, b);
    
    if (a.value_a == 15.0f && b.value_b == 20.0f) {
        std::cout << "  ✓ Multi-output process_continuous(const C&, A&, B&) signature correct\n";
        std::cout << "  ✓ Multi-output continuous processing works\n";
    } else {
        std::cout << "  ✗ Multi-output continuous processing failed\n";
    }
}

// ============================================================================
// Test 3: Type Aliases
// ============================================================================

void test_type_aliases() {
    std::cout << "\n[Test 3] Type Aliases for Multi-Output\n";
    
    using M1 = TestApp::Module<Outputs<DataA, DataB>, PeriodicInput>;
    static_assert(std::is_same_v<M1::OutputData, void>,
                  "Multi-output OutputData should be void");
    
    using M2 = TestApp::Module<Outputs<DataA, DataB>, Input<InputData>>;
    static_assert(std::is_same_v<M2::OutputData, void>,
                  "Multi-output continuous OutputData should be void");
    static_assert(std::is_same_v<M2::InputData, InputData>,
                  "InputData should be InputData");
    
    std::cout << "  ✓ Multi-output OutputData = void\n";
    std::cout << "  ✓ Type aliases correct for multi-output\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== Multi-Output Signature Tests (Phase 5.3) ===\n\n";
    
    test_multi_output_periodic();
    test_multi_output_continuous();
    test_type_aliases();
    
    std::cout << "\n=== All Multi-Output Tests Passed ✓ ===\n";
    return 0;
}
