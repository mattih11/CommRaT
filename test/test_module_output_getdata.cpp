/**
 * @file test_module_output_getdata.cpp
 * @brief Test GetData request handling in ModuleOutput
 */

#include "commrat/commrat.hpp"
#include <corerat/messaging/wire_message.hpp>
// message_id now via corerat/messaging/message_id.hpp (included transitively)
#include "commrat/messaging/message_registry.hpp"
#include "commrat/messaging/message_helpers.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/module/io/output/module_output.hpp"
// timestamp now via corerat/platform/timestamp.hpp (included transitively)
#include <cassert>
#include <iostream>

using namespace commrat;

// Type aliases
using SystemId = uint32_t;
using InstanceId = uint32_t;

// Test data type
struct SensorData {
    float value;
    uint32_t sensor_id;
};

// Test application
using TestApp = commrat::CommRaT<
    Message::Data<SensorData>
>;

void test_get_data_exact_match() {
    std::cout << "Test: GetData with exact timestamp match...\n";
    
    // Create output
    ModuleOutput<TestApp, SensorData> output(
        SystemId{10}, 
        InstanceId{1},
        Milliseconds(50)  // 50ms default tolerance
    );
    
    // Publish test data
    Timestamp t1 = 1000000000ULL;  // 1 second
    Timestamp t2 = 2000000000ULL;  // 2 seconds
    Timestamp t3 = 3000000000ULL;  // 3 seconds
    
    output.publish(SensorData{.value = 10.0f, .sensor_id = 1}, t1);
    output.publish(SensorData{.value = 20.0f, .sensor_id = 2}, t2);
    output.publish(SensorData{.value = 30.0f, .sensor_id = 3}, t3);
    
    // Request data with exact timestamp match
    GetDataRequestPayload<SensorData> req{
        .target_timestamp = 2000000000,  // Match t2 exactly
        .tolerance_ns = 100'000'000,     // 100ms tolerance
        .interpolation_mode = 0          // NEAREST
    };
    
    auto reply = output.handle_get_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 20.0f);
    assert(reply.data.sensor_id == 2);
    assert(reply.data_timestamp == 2000000000);
    assert(reply.timestamp_delta_ns == 0);  // Exact match
    
    std::cout << "  PASS: Exact match found\n";
}

void test_get_data_nearest() {
    std::cout << "Test: GetData with NEAREST interpolation...\n";
    
    ModuleOutput<TestApp, SensorData> output(
        SystemId{10}, 
        InstanceId{1},
        Milliseconds(50)
    );
    
    // Publish test data
    Timestamp t1 = 1000000000ULL;
    Timestamp t2 = 3000000000ULL;
    
    output.publish(SensorData{.value = 10.0f, .sensor_id = 1}, t1);
    output.publish(SensorData{.value = 30.0f, .sensor_id = 3}, t2);
    
    // Request timestamp between t1 and t2 (closer to t1)
    GetDataRequestPayload<SensorData> req{
        .target_timestamp = 1500000000,  // 1.5 seconds (closer to t1)
        .tolerance_ns = 1'000'000'000,   // 1 second tolerance
        .interpolation_mode = 0          // NEAREST
    };
    
    auto reply = output.handle_get_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 10.0f);  // Should match t1
    assert(reply.data_timestamp == 1000000000);
    
    std::cout << "  PASS: Nearest match found (t1)\n";
}

void test_get_data_not_found() {
    std::cout << "Test: GetData with no match within tolerance...\n";
    
    ModuleOutput<TestApp, SensorData> output(
        SystemId{10}, 
        InstanceId{1},
        Milliseconds(50)
    );
    
    // Publish test data
    Timestamp t1 = 1000000000ULL;
    output.publish(SensorData{.value = 10.0f, .sensor_id = 1}, t1);
    
    // Request timestamp far in the future with tight tolerance
    GetDataRequestPayload<SensorData> req{
        .target_timestamp = 5000000000,  // 5 seconds
        .tolerance_ns = 10'000'000,      // 10ms tolerance
        .interpolation_mode = 0
    };
    
    auto reply = output.handle_get_data_request(req);
    
    assert(!reply.found);
    assert(reply.data_timestamp == 0);
    
    std::cout << "  PASS: No match found (outside tolerance)\n";
}

void test_get_next_data() {
    std::cout << "Test: GetNextData returns latest message...\n";
    
    ModuleOutput<TestApp, SensorData> output(
        SystemId{10}, 
        InstanceId{1},
        Milliseconds(50)
    );
    
    // Publish test data
    Timestamp t1 = 1000000000ULL;
    Timestamp t2 = 2000000000ULL;
    Timestamp t3 = 3000000000ULL;
    
    output.publish(SensorData{.value = 10.0f, .sensor_id = 1}, t1);
    output.publish(SensorData{.value = 20.0f, .sensor_id = 2}, t2);
    output.publish(SensorData{.value = 30.0f, .sensor_id = 3}, t3);
    
    // Request next available data
    GetNextDataRequestPayload<SensorData> req{};
    
    auto reply = output.handle_get_next_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 30.0f);  // Latest data
    assert(reply.data.sensor_id == 3);
    assert(reply.data_timestamp == 3000000000);
    
    std::cout << "  PASS: Latest message returned\n";
}

void test_get_next_data_empty_buffer() {
    std::cout << "Test: GetNextData with empty buffer...\n";
    
    ModuleOutput<TestApp, SensorData> output(
        SystemId{10}, 
        InstanceId{1},
        Milliseconds(50)
    );
    
    // Request next data from empty buffer
    GetNextDataRequestPayload<SensorData> req{};
    
    auto reply = output.handle_get_next_data_request(req);
    
    assert(!reply.found);
    assert(reply.data_timestamp == 0);
    
    std::cout << "  PASS: Empty buffer handled correctly\n";
}

void test_get_data_wrap_around() {
    std::cout << "Test: GetData with wrap-around buffer...\n";
    
    // Create output with small buffer size to force wrap-around
    ModuleOutput<TestApp, SensorData, 10> output(
        SystemId{10}, 
        InstanceId{1},
        Milliseconds(50)
    );
    
    // Publish test data
    Timestamp t1 = 1000000000ULL;
    Timestamp t2 = 2000000000ULL;
    Timestamp t3 = 3000000000ULL;
    
    output.publish(SensorData{.value = 10.0f, .sensor_id = 1}, t1);
    output.publish(SensorData{.value = 20.0f, .sensor_id = 2}, t2);
    output.publish(SensorData{.value = 30.0f, .sensor_id = 3}, t3);
    
    // Request timestamp that wraps around buffer
    GetDataRequestPayload<SensorData> req{
        .target_timestamp = 1500000000,  // 1.5 seconds (closer to t1)
        .tolerance_ns = 1'000'000'000,   // 1 second tolerance
        .interpolation_mode = 0          // NEAREST
    };
    
    auto reply = output.handle_get_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 10.0f);  // Should match t1
    assert(reply.data_timestamp == 1000000000);

    // Request timestamp that wraps around buffer
    GetDataRequestPayload<SensorData> req_t1{
        .target_timestamp = 1100000000,  // 1.1 seconds (closer to t1)
        .tolerance_ns = 150'000'000,   // 150 millisecond tolerance
        .interpolation_mode = 0          // NEAREST
    };
    
    reply = output.handle_get_data_request(req_t1);
    
    assert(reply.found);
    assert(reply.data.value == 10.0f);  // Should match t1
    assert(reply.data_timestamp == 1000000000);
    
    // Now publish more data to have t1 as oldest in buffer and full buffer (10 messages)
    for (int i = 0; i < 7; ++i) {
        output.publish(SensorData{.value = 40.0f + static_cast<float>(i),
            .sensor_id = static_cast<uint32_t>(4 + i)}, t3 + (i + 1) * 1000000000ULL);
    }
    reply = output.handle_get_data_request(req);
    
    assert(reply.found);
    assert(reply.data.value == 10.0f);  // Should match t1
    assert(reply.data_timestamp == 1000000000);

    // Publish one more message to wrap around buffer and overwrite t1
    output.publish(SensorData{.value = 50.0f,
        .sensor_id = 7}, t3 + (7 + 1) * 1000000000ULL);
    reply = output.handle_get_data_request(req_t1);
    assert(!reply.found);

    reply = output.handle_get_data_request(req);
    assert(reply.found);
    assert(reply.data.value == 20.0f);  // Should match t2 now (t1 overwritten)
    assert(reply.data_timestamp == 2000000000);

    // Publish one more message to wrap around buffer and overwrite t2
    output.publish(SensorData{.value = 60.0f,
        .sensor_id = 8}, t3 + (8 + 1) * 1000000000ULL);
    // now t2 should be overwritten, so req should not find t1 or t2
    reply = output.handle_get_data_request(req);
    assert(!reply.found);
    std::cout << "  PASS: Wrap-around buffer handled correctly\n";
}

int main() {
    std::cout << "\n=== ModuleOutput GetData Handler Tests ===\n\n";
    
    test_get_data_exact_match();
    test_get_data_nearest();
    test_get_data_not_found();
    test_get_next_data();
    test_get_next_data_empty_buffer();
    test_get_data_wrap_around();    
    std::cout << "\n=== All tests passed! ===\n";
    return 0;
}
