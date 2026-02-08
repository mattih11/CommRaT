/**
 * @file test_primary_input.cpp
 * @brief Test Phase 6 PrimaryInput<T> type traits and validation
 * 
 * Validates:
 * - PrimaryInput<T> detection
 * - PrimaryInputIndex computation
 * - Compile-time validation of primary in inputs list
 */

#include "commrat/io_spec.hpp"
#include <iostream>
#include <cassert>

// Test message types
struct IMUData { int id; };
struct GPSData { int id; };
struct LidarData { int id; };

using namespace commrat;

int main() {
    std::cout << "=== Phase 6 PrimaryInput Type Traits Tests ===\n\n";

    // Test 1: PrimaryInput detection
    {
        std::cout << "Test 1: PrimaryInput<T> detection\n";
        
        static_assert(is_primary_input_v<PrimaryInput<IMUData>>, 
                      "PrimaryInput<IMUData> should be detected");
        
        static_assert(!is_primary_input_v<IMUData>,
                      "Raw type should not be primary input");
        
        static_assert(!is_primary_input_v<Input<IMUData>>,
                      "Input<T> is not primary input");
        
        std::cout << "  PASS: PrimaryInput<T> correctly detected\n\n";
    }

    // Test 2: Primary input index extraction (first position)
    {
        std::cout << "Test 2: PrimaryInputIndex - first position\n";
        
        using InputTypes = Inputs<IMUData, GPSData, LidarData>;
        using Primary = PrimaryInput<IMUData>;
        
        constexpr size_t idx = PrimaryInputIndex_v<IMUData, typename InputTypes::PayloadTypes>;
        static_assert(idx == 0, "IMUData should be at index 0");
        
        std::cout << "  Primary: IMUData, Index: " << idx << " (expected: 0)\n";
        std::cout << "  PASS: First input correctly indexed\n\n";
    }

    // Test 3: Primary input index extraction (middle position)
    {
        std::cout << "Test 3: PrimaryInputIndex - middle position\n";
        
        using InputTypes = Inputs<IMUData, GPSData, LidarData>;
        using Primary = PrimaryInput<GPSData>;
        
        constexpr size_t idx = PrimaryInputIndex_v<GPSData, typename InputTypes::PayloadTypes>;
        static_assert(idx == 1, "GPSData should be at index 1");
        
        std::cout << "  Primary: GPSData, Index: " << idx << " (expected: 1)\n";
        std::cout << "  PASS: Middle input correctly indexed\n\n";
    }

    // Test 4: Primary input index extraction (last position)
    {
        std::cout << "Test 4: PrimaryInputIndex - last position\n";
        
        using InputTypes = Inputs<IMUData, GPSData, LidarData>;
        using Primary = PrimaryInput<LidarData>;
        
        constexpr size_t idx = PrimaryInputIndex_v<LidarData, typename InputTypes::PayloadTypes>;
        static_assert(idx == 2, "LidarData should be at index 2");
        
        std::cout << "  Primary: LidarData, Index: " << idx << " (expected: 2)\n";
        std::cout << "  PASS: Last input correctly indexed\n\n";
    }

    // Test 5: Validate primary input is in inputs list
    {
        std::cout << "Test 5: ValidatePrimaryInput - valid primary\n";
        
        using InputTypes = Inputs<IMUData, GPSData, LidarData>;
        using Primary = PrimaryInput<GPSData>;
        
        static_assert(ValidatePrimaryInput_v<Primary, InputTypes>,
                      "GPSData is in Inputs<IMUData, GPSData, LidarData>");
        
        std::cout << "  Primary: GPSData in Inputs<IMUData, GPSData, LidarData>\n";
        std::cout << "  PASS: Valid primary input validated\n\n";
    }

    // Test 6: Multiple inputs count
    {
        std::cout << "Test 6: Inputs<Ts...> count validation\n";
        
        using InputTypes = Inputs<IMUData, GPSData, LidarData>;
        
        static_assert(InputTypes::count == 3, "Should have 3 inputs");
        static_assert(InputCount_v<InputTypes> == 3, "InputCount should be 3");
        static_assert(is_multi_input_v<InputTypes>, "Should be multi-input");
        
        std::cout << "  Inputs<IMUData, GPSData, LidarData>::count = " << InputTypes::count << "\n";
        std::cout << "  PASS: Multiple inputs count correct\n\n";
    }

    // Test 7: Input payload types extraction
    {
        std::cout << "Test 7: Input payload types extraction\n";
        
        using InputTypes = Inputs<IMUData, GPSData, LidarData>;
        using Payloads = InputPayloadTypes_t<InputTypes>;
        
        static_assert(std::is_same_v<Payloads, std::tuple<IMUData, GPSData, LidarData>>,
                      "Payload types should match");
        
        static_assert(std::tuple_size_v<Payloads> == 3, "Should have 3 payload types");
        
        std::cout << "  Extracted: tuple<IMUData, GPSData, LidarData>\n";
        std::cout << "  PASS: Payload types correctly extracted\n\n";
    }

    std::cout << "=== All Phase 6 Type Trait Tests Passed! ===\n";
    std::cout << "\nPhase 6.1 Complete: PrimaryInput<T> infrastructure ready\n";
    std::cout << "Next: Implement RingBuffer with timestamped history\n";

    return 0;
}

// Compile-time error tests (uncomment to verify error messages):

/*
// ERROR Test: Primary type not in inputs list
using BadInputs = Inputs<IMUData, GPSData>;
using BadPrimary = PrimaryInput<LidarData>;  // LidarData not in BadInputs!

static_assert(ValidatePrimaryInput_v<BadPrimary, BadInputs>,
              "Should fail: LidarData not in Inputs<IMUData, GPSData>");
*/

/*
// ERROR Test: Using PrimaryInput with single input
using SingleInput = Input<IMUData>;
using WrongPrimary = PrimaryInput<IMUData>;

static_assert(ValidatePrimaryInput_v<WrongPrimary, SingleInput>,
              "Should fail: Input<T> is not Inputs<Ts...>");
*/
