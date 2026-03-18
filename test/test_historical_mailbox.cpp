/**
 * @file test_historical_mailbox.cpp
 * @brief Test ModuleOutput timestamped buffering for get_data synchronization
 * 
 * Architecture:
 * - ModuleOutput maintains TimestampedRingBuffer for all published data
 * - Timestamps stored in TimsHeader only (not in payloads)
 * - handle_get_data_request() provides temporal synchronization for SyncedInput
 * - handle_get_next_data_request() returns latest data
 */

#include "commrat/commrat.hpp"
#include "commrat/module/io/output/module_output.hpp"
#include <iostream>
#include <cassert>

using namespace commrat;

// Test message types (clean POD, no timestamp fields)
struct SensorData {
    uint32_t sensor_id;
    float value;
};

struct ControlData {
    float setpoint;
};

// Modern CommRaT application definition
using TestApp = CommRaT<
    Message::Data<SensorData>,
    Message::Data<ControlData>
>;

void test_basic_construction() {
    std::cout << "Test 1: Construction and initialization\n";
    
    ModuleOutput<TestApp, SensorData> output(
        10,  // system_id
        1,   // instance_id
        Milliseconds(50)  // default tolerance
    );
    
    std::cout << "  PASS: ModuleOutput constructed successfully\n";
}

void test_publish_and_get_data() {
    std::cout << "\nTest 2: Publish with automatic buffering and get_data retrieval\n";
    
    ModuleOutput<TestApp, SensorData> output(10, 1, Milliseconds(50));
    
    // Publish data with explicit timestamp
    Timestamp t1 = 1000000000ULL;  // 1 second
    output.publish(SensorData{.sensor_id = 42, .value = 25.5f}, t1);
    
    // Request exact timestamp match
    GetDataRequestPayload<SensorData> req{
        .target_timestamp = t1,
        .tolerance_ns = 100'000'000,  // 100ms tolerance
        .interpolation_mode = 0        // NEAREST
    };
    
    auto reply = output.handle_get_data_request(req);
    
    assert(reply.found);
    assert(reply.data.sensor_id == 42);
    assert(reply.data.value == 25.5f);
    assert(reply.data_timestamp == t1);
    assert(reply.timestamp_delta_ns == 0);  // Exact match
    
    std::cout << "  PASS: Publish automatically stores with timestamp\n";
    std::cout << "  PASS: get_data retrieves buffered data\n";
}

void test_get_data_nearest() {
    std::cout << "\nTest 3: get_data with NEAREST interpolation\n";
    
    ModuleOutput<TestApp, SensorData> output(10, 1, Milliseconds(50));
    
    // Publish messages with known timestamps
    Timestamp t1 = 1000000000ULL;  // 1 second
    Timestamp t2 = 2000000000ULL;  // 2 seconds
    Timestamp t3 = 3000000000ULL;  // 3 seconds
    
    output.publish(SensorData{.sensor_id = 1, .value = 10.0f}, t1);
    output.publish(SensorData{.sensor_id = 2, .value = 20.0f}, t2);
    output.publish(SensorData{.sensor_id = 3, .value = 30.0f}, t3);
    
    // Request timestamp between t1 and t2 (closer to t1)
    GetDataRequestPayload<SensorData> req{
        .target_timestamp = 1400000000,  // 1.4 seconds
        .tolerance_ns = 1'000'000'000,    // 1 second tolerance
        .interpolation_mode = 0           // NEAREST
    };
    
    auto reply = output.handle_get_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 10.0f);  // Should match t1 (nearest)
    assert(reply.data_timestamp == t1);
    
    // Request timestamp closer to t3
    req.target_timestamp = 2800000000;  // 2.8 seconds
    reply = output.handle_get_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 30.0f);  // Should match t3 (nearest)
    assert(reply.data_timestamp == t3);
    
    std::cout << "  PASS: NEAREST mode selects closest timestamp\n";
}

void test_tolerance_enforcement() {
    std::cout << "\nTest 4: Tolerance enforcement\n";
    
    ModuleOutput<TestApp, SensorData> output(10, 1, Milliseconds(20));  // 20ms tolerance
    
    // Publish message
    Timestamp t1 = 1000000000ULL;
    output.publish(SensorData{.sensor_id = 1, .value = 100.0f}, t1);
    
    // Within tolerance (10ms away, tolerance is 20ms)
    GetDataRequestPayload<SensorData> req_within{
        .target_timestamp = t1 + 10'000'000,  // +10ms
        .tolerance_ns = 20'000'000,            // 20ms
        .interpolation_mode = 0
    };
    auto within = output.handle_get_data_request(req_within);
    assert(within.found);
    
    // Outside tolerance (30ms away, tolerance is 20ms)
    GetDataRequestPayload<SensorData> req_outside{
        .target_timestamp = t1 + 30'000'000,  // +30ms
        .tolerance_ns = 20'000'000,            // 20ms
        .interpolation_mode = 0
    };
    auto outside = output.handle_get_data_request(req_outside);
    assert(!outside.found);
    
    // Override tolerance (allow 50ms)
    GetDataRequestPayload<SensorData> req_override{
        .target_timestamp = t1 + 30'000'000,  // +30ms
        .tolerance_ns = 50'000'000,            // 50ms override
        .interpolation_mode = 0
    };
    auto override_tol = output.handle_get_data_request(req_override);
    assert(override_tol.found);
    
    std::cout << "  PASS: Tolerance correctly enforced\n";
}

void test_multiple_message_types() {
    std::cout << "\nTest 5: Different ModuleOutput instances for different types\n";
    
    ModuleOutput<TestApp, SensorData> sensor_output(10, 1, Milliseconds(50));
    ModuleOutput<TestApp, ControlData> control_output(20, 1, Milliseconds(50));
    
    // Publish both message types
    Timestamp t1 = 1000000000ULL;
    Timestamp t2 = 2000000000ULL;
    
    sensor_output.publish(SensorData{.sensor_id = 10, .value = 50.0f}, t1);
    control_output.publish(ControlData{.setpoint = 75.0f}, t2);
    
    // get_data for each type
    GetDataRequestPayload<SensorData> sensor_req{
        .target_timestamp = t1,
        .tolerance_ns = 100'000'000,
        .interpolation_mode = 0
    };
    auto sensor_reply = sensor_output.handle_get_data_request(sensor_req);
    
    GetDataRequestPayload<ControlData> control_req{
        .target_timestamp = t2,
        .tolerance_ns = 100'000'000,
        .interpolation_mode = 0
    };
    auto control_reply = control_output.handle_get_data_request(control_req);
    
    assert(sensor_reply.found);
    assert(sensor_reply.data.sensor_id == 10);
    assert(sensor_reply.data.value == 50.0f);
    
    assert(control_reply.found);
    assert(control_reply.data.setpoint == 75.0f);
    
    std::cout << "  PASS: Multiple message types handled correctly\n";
}

void test_get_next_data() {
    std::cout << "\nTest 6: GetNextData returns latest buffered message\n";
    
    ModuleOutput<TestApp, SensorData> output(10, 1, Milliseconds(50));
    
    // Publish messages
    Timestamp t1 = 1000000000ULL;
    Timestamp t2 = 2000000000ULL;
    Timestamp t3 = 3000000000ULL;
    
    output.publish(SensorData{.sensor_id = 1, .value = 10.0f}, t1);
    output.publish(SensorData{.sensor_id = 2, .value = 20.0f}, t2);
    output.publish(SensorData{.sensor_id = 3, .value = 30.0f}, t3);
    
    // Request next available data
    GetNextDataRequestPayload<SensorData> req{};
    auto reply = output.handle_get_next_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 30.0f);  // Latest data
    assert(reply.data.sensor_id == 3);
    assert(reply.data_timestamp == t3);
    
    std::cout << "  PASS: Latest message returned\n";
}

void test_buffer_wrap_around() {
    std::cout << "\nTest 7: Buffer wrap-around behavior\n";
    
    // Small buffer to force wrap-around
    ModuleOutput<TestApp, SensorData, 5> output(10, 1, Milliseconds(50));
    
    // Fill buffer beyond capacity
    for (uint32_t i = 0; i < 10; ++i) {
        Timestamp t = (i + 1) * 1000000000ULL;
        output.publish(SensorData{.sensor_id = i, .value = float(i)}, t);
    }
    
    // First 5 messages should be overwritten
    GetDataRequestPayload<SensorData> req_old{
        .target_timestamp = 1000000000,  // First message
        .tolerance_ns = 100'000'000,
        .interpolation_mode = 0
    };
    auto old_reply = output.handle_get_data_request(req_old);
    assert(!old_reply.found);  // Overwritten
    
    // Recent messages should still be available
    GetDataRequestPayload<SensorData> req_recent{
        .target_timestamp = 9000000000,  // 9th message (within buffer)
        .tolerance_ns = 100'000'000,
        .interpolation_mode = 0
    };
    auto recent_reply = output.handle_get_data_request(req_recent);
    assert(recent_reply.found);
    assert(recent_reply.data.sensor_id == 8);
    
    std::cout << "  PASS: Wrap-around buffer correctly evicts old data\n";
}

int main() {
    std::cout << "\n=== ModuleOutput Timestamped Buffering Tests ===\n";
    std::cout << "Architecture: Automatic history buffering with TimsHeader timestamps\n\n";
    
    test_basic_construction();
    test_publish_and_get_data();
    test_get_data_nearest();
    test_tolerance_enforcement();
    test_multiple_message_types();
    test_get_next_data();
    test_buffer_wrap_around();
    
    std::cout << "\n=== All Tests Passed! ===\n\n";
    std::cout << "ModuleOutput validated:\n";
    std::cout << "  - Automatic buffering on publish()\n";
    std::cout << "  - TimsHeader.timestamp used (no payload timestamps)\n";
    std::cout << "  - handle_get_data_request() with NEAREST interpolation\n";
    std::cout << "  - handle_get_next_data_request() for latest data\n";
    std::cout << "  - Tolerance-based matching\n";
    std::cout << "  - Separate ModuleOutput per message type\n";
    std::cout << "  - RingBuffer wrap-around behavior\n\n";
    
    return 0;
}
