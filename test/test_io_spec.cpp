/**
 * @file test_io_spec.cpp
 * @brief Unit tests for I/O specification types
 * 
 * Tests compile-time properties of Input/Output specifications.
 */

#include <commrat/io_spec.hpp>
#include <iostream>
#include <cassert>

using namespace commrat;

// Test payload types
struct TempData { float temp; };
struct PressureData { float pressure; };
struct HumidityData { float humidity; };
struct FusedData { float temp, pressure, humidity; };
struct DiagnosticsData { int error_count; };

// ============================================================================
// Output Specification Tests
// ============================================================================

void test_single_output() {
    std::cout << "Testing Output<T>...\n";
    
    using OutSpec = Output<TempData>;
    static_assert(OutSpec::count == 1, "Single output should have count=1");
    static_assert(std::is_same_v<OutSpec::PayloadType, TempData>, "PayloadType mismatch");
    static_assert(is_single_output_v<OutSpec>, "Should be detected as single output");
    static_assert(!is_multi_output_v<OutSpec>, "Should not be multi output");
    static_assert(!is_no_output_v<OutSpec>, "Should not be no output");
    static_assert(ValidOutputSpec<OutSpec>, "Should be valid output spec");
    
    std::cout << "  ✓ Output<T> count: " << OutSpec::count << "\n";
    std::cout << "  ✓ Output<T> validation passed\n";
}

void test_multiple_outputs() {
    std::cout << "Testing Outputs<Ts...>...\n";
    
    using OutSpec = Outputs<TempData, PressureData, HumidityData>;
    static_assert(OutSpec::count == 3, "Multi output should have count=3");
    static_assert(std::is_same_v<OutSpec::PayloadTypes, 
                                 std::tuple<TempData, PressureData, HumidityData>>,
                  "PayloadTypes tuple mismatch");
    static_assert(!is_single_output_v<OutSpec>, "Should not be single output");
    static_assert(is_multi_output_v<OutSpec>, "Should be detected as multi output");
    static_assert(!is_no_output_v<OutSpec>, "Should not be no output");
    static_assert(ValidOutputSpec<OutSpec>, "Should be valid output spec");
    
    std::cout << "  ✓ Outputs<Ts...> count: " << OutSpec::count << "\n";
    std::cout << "  ✓ Outputs<Ts...> validation passed\n";
}

void test_no_output() {
    std::cout << "Testing NoOutput...\n";
    
    static_assert(NoOutput::count == 0, "NoOutput should have count=0");
    static_assert(!is_single_output_v<NoOutput>, "Should not be single output");
    static_assert(!is_multi_output_v<NoOutput>, "Should not be multi output");
    static_assert(is_no_output_v<NoOutput>, "Should be detected as no output");
    static_assert(ValidOutputSpec<NoOutput>, "Should be valid output spec");
    
    std::cout << "  ✓ NoOutput count: " << NoOutput::count << "\n";
    std::cout << "  ✓ NoOutput validation passed\n";
}

// ============================================================================
// Input Specification Tests
// ============================================================================

void test_single_input() {
    std::cout << "Testing Input<T>...\n";
    
    using InSpec = Input<TempData>;
    static_assert(InSpec::count == 1, "Single input should have count=1");
    static_assert(std::is_same_v<InSpec::PayloadType, TempData>, "PayloadType mismatch");
    static_assert(is_single_input_v<InSpec>, "Should be detected as single input");
    static_assert(!is_multi_input_v<InSpec>, "Should not be multi input");
    static_assert(ValidInputSpec<InSpec>, "Should be valid input spec");
    
    std::cout << "  ✓ Input<T> count: " << InSpec::count << "\n";
    std::cout << "  ✓ Input<T> validation passed\n";
}

void test_multiple_inputs() {
    std::cout << "Testing Inputs<Ts...>...\n";
    
    using InSpec = Inputs<TempData, PressureData, HumidityData>;
    static_assert(InSpec::count == 3, "Multi input should have count=3");
    static_assert(std::is_same_v<InSpec::PayloadTypes,
                                 std::tuple<TempData, PressureData, HumidityData>>,
                  "PayloadTypes tuple mismatch");
    static_assert(!is_single_input_v<InSpec>, "Should not be single input");
    static_assert(is_multi_input_v<InSpec>, "Should be detected as multi input");
    static_assert(ValidInputSpec<InSpec>, "Should be valid input spec");
    
    std::cout << "  ✓ Inputs<Ts...> count: " << InSpec::count << "\n";
    std::cout << "  ✓ Inputs<Ts...> validation passed\n";
}

void test_legacy_inputs() {
    std::cout << "Testing legacy input types...\n";
    
    static_assert(is_periodic_input_v<PeriodicInput>, "PeriodicInput detection failed");
    static_assert(is_loop_input_v<LoopInput>, "LoopInput detection failed");
    static_assert(is_continuous_input_legacy_v<ContinuousInput<TempData>>, 
                  "ContinuousInput detection failed");
    static_assert(ValidInputSpec<PeriodicInput>, "PeriodicInput should be valid");
    static_assert(ValidInputSpec<LoopInput>, "LoopInput should be valid");
    static_assert(ValidInputSpec<ContinuousInput<TempData>>, "ContinuousInput should be valid");
    
    std::cout << "  ✓ PeriodicInput validation passed\n";
    std::cout << "  ✓ LoopInput validation passed\n";
    std::cout << "  ✓ ContinuousInput<T> validation passed\n";
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

void test_input_normalization() {
    std::cout << "Testing input normalization...\n";
    
    // ContinuousInput<T> should map to Input<T>
    static_assert(std::is_same_v<NormalizeInput_t<ContinuousInput<TempData>>,
                                 Input<TempData>>,
                  "ContinuousInput normalization failed");
    
    // Input<T> should pass through unchanged
    static_assert(std::is_same_v<NormalizeInput_t<Input<TempData>>,
                                 Input<TempData>>,
                  "Input<T> normalization failed");
    
    // PeriodicInput should pass through unchanged
    static_assert(std::is_same_v<NormalizeInput_t<PeriodicInput>,
                                 PeriodicInput>,
                  "PeriodicInput normalization failed");
    
    std::cout << "  ✓ ContinuousInput<T> -> Input<T> mapping works\n";
    std::cout << "  ✓ Input normalization passed\n";
}

void test_output_normalization() {
    std::cout << "Testing output normalization...\n";
    
    // Raw type T should map to Output<T>
    static_assert(std::is_same_v<NormalizeOutput_t<TempData>,
                                 Output<TempData>>,
                  "Raw type normalization failed");
    
    // Output<T> should pass through unchanged
    static_assert(std::is_same_v<NormalizeOutput_t<Output<TempData>>,
                                 Output<TempData>>,
                  "Output<T> normalization failed");
    
    // Outputs<Ts...> should pass through unchanged
    static_assert(std::is_same_v<NormalizeOutput_t<Outputs<TempData, PressureData>>,
                                 Outputs<TempData, PressureData>>,
                  "Outputs<Ts...> normalization failed");
    
    std::cout << "  ✓ Raw type -> Output<T> mapping works\n";
    std::cout << "  ✓ Output normalization passed\n";
}

// ============================================================================
// Type Extraction Tests
// ============================================================================

void test_payload_type_extraction() {
    std::cout << "Testing payload type extraction...\n";
    
    // Input payload types
    static_assert(std::is_same_v<InputPayloadTypes_t<Input<TempData>>,
                                 std::tuple<TempData>>,
                  "Single input payload extraction failed");
    
    static_assert(std::is_same_v<InputPayloadTypes_t<Inputs<TempData, PressureData>>,
                                 std::tuple<TempData, PressureData>>,
                  "Multi input payload extraction failed");
    
    // Output payload types
    static_assert(std::is_same_v<OutputPayloadTypes_t<Output<FusedData>>,
                                 std::tuple<FusedData>>,
                  "Single output payload extraction failed");
    
    static_assert(std::is_same_v<OutputPayloadTypes_t<Outputs<FusedData, DiagnosticsData>>,
                                 std::tuple<FusedData, DiagnosticsData>>,
                  "Multi output payload extraction failed");
    
    // Single type extraction
    static_assert(std::is_same_v<SingleOutputType_t<Output<TempData>>, TempData>,
                  "Single output type extraction failed");
    
    static_assert(std::is_same_v<SingleInputType_t<Input<TempData>>, TempData>,
                  "Single input type extraction failed");
    
    static_assert(std::is_same_v<SingleInputType_t<ContinuousInput<TempData>>, TempData>,
                  "Legacy continuous input type extraction failed");
    
    std::cout << "  ✓ Input payload type extraction works\n";
    std::cout << "  ✓ Output payload type extraction works\n";
    std::cout << "  ✓ Single type extraction works\n";
}

// ============================================================================
// Concept Tests
// ============================================================================

void test_concepts() {
    std::cout << "Testing concepts...\n";
    
    // ValidOutputSpec
    static_assert(ValidOutputSpec<Output<TempData>>, "Output<T> should satisfy concept");
    static_assert(ValidOutputSpec<Outputs<TempData, PressureData>>, "Outputs<Ts...> should satisfy concept");
    static_assert(ValidOutputSpec<NoOutput>, "NoOutput should satisfy concept");
    
    // ValidInputSpec
    static_assert(ValidInputSpec<Input<TempData>>, "Input<T> should satisfy concept");
    static_assert(ValidInputSpec<Inputs<TempData, PressureData>>, "Inputs<Ts...> should satisfy concept");
    static_assert(ValidInputSpec<PeriodicInput>, "PeriodicInput should satisfy concept");
    static_assert(ValidInputSpec<LoopInput>, "LoopInput should satisfy concept");
    static_assert(ValidInputSpec<ContinuousInput<TempData>>, "ContinuousInput<T> should satisfy concept");
    
    // PeriodicOrLoop
    static_assert(PeriodicOrLoop<PeriodicInput>, "PeriodicInput should satisfy PeriodicOrLoop");
    static_assert(PeriodicOrLoop<LoopInput>, "LoopInput should satisfy PeriodicOrLoop");
    static_assert(!PeriodicOrLoop<Input<TempData>>, "Input<T> should not satisfy PeriodicOrLoop");
    
    // HasContinuousInput
    static_assert(HasContinuousInput<Input<TempData>>, "Input<T> should have continuous input");
    static_assert(HasContinuousInput<Inputs<TempData, PressureData>>, "Inputs<Ts...> should have continuous input");
    static_assert(HasContinuousInput<ContinuousInput<TempData>>, "ContinuousInput<T> should have continuous input");
    static_assert(!HasContinuousInput<PeriodicInput>, "PeriodicInput should not have continuous input");
    
    std::cout << "  ✓ ValidOutputSpec concept works\n";
    std::cout << "  ✓ ValidInputSpec concept works\n";
    std::cout << "  ✓ PeriodicOrLoop concept works\n";
    std::cout << "  ✓ HasContinuousInput concept works\n";
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "===========================================\n";
    std::cout << "CommRaT I/O Specification Tests\n";
    std::cout << "===========================================\n\n";
    
    try {
        test_single_output();
        std::cout << "\n";
        
        test_multiple_outputs();
        std::cout << "\n";
        
        test_no_output();
        std::cout << "\n";
        
        test_single_input();
        std::cout << "\n";
        
        test_multiple_inputs();
        std::cout << "\n";
        
        test_legacy_inputs();
        std::cout << "\n";
        
        test_input_normalization();
        std::cout << "\n";
        
        test_output_normalization();
        std::cout << "\n";
        
        test_payload_type_extraction();
        std::cout << "\n";
        
        test_concepts();
        std::cout << "\n";
        
        std::cout << "===========================================\n";
        std::cout << "✅ ALL TESTS PASSED!\n";
        std::cout << "===========================================\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ TEST FAILED: " << e.what() << "\n";
        return 1;
    }
}
