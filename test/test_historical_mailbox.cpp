/**
 * @file test_historical_mailbox.cpp
 * @brief Test suite for HistoricalMailbox (Phase 6.3 + Phase 6.10)
 * 
 * Phase 6.10: Timestamps stored ONLY in TimsHeader, not in payloads.
 * HistoricalMailbox automatically extracts TimsMessage.header.timestamp
 * and stores in history buffer for getData() synchronization.
 */

#include <commrat/mailbox/historical_mailbox.hpp>
#include <commrat/commrat.hpp>
#include <iostream>
#include <thread>
#include <atomic>

// Test message types (NO timestamp fields - Phase 6.10)
struct SensorData {
    uint32_t sensor_id;
    float value;
};

struct ControlData {
    float setpoint;
};

// Define test registry
using TestRegistry = commrat::MessageRegistry<
    commrat::MessageDefinition<SensorData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<ControlData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>
>;

using namespace commrat;

int main() {
    std::cout << "=== Phase 6.3/6.10 HistoricalMailbox Tests ===\n";
    std::cout << "Architecture: Timestamps in TimsHeader only\n\n";
    
    // Test 1: Basic construction
    std::cout << "Test 1: Construction and initialization\n";
    {
        MailboxConfig config{
            .mailbox_id = 100,
            .max_message_size = 1024
        };
        
        HistoricalMailbox<TestRegistry, 50> mailbox(config, Milliseconds(50));
        
        std::cout << "  ✓ HistoricalMailbox constructed successfully\n";
    }
    
    // Test 2: Receive and automatic history storage
    std::cout << "\nTest 2: Receive with automatic history storage (header timestamp)\n";
    {
        MailboxConfig config1{
            .mailbox_id = 101,
            .max_message_size = 1024
        };
        
        MailboxConfig config2{
            .mailbox_id = 102,
            .max_message_size = 1024
        };
        
        HistoricalMailbox<TestRegistry, 50> sender_mbx(config1);
        HistoricalMailbox<TestRegistry, 50> receiver_mbx(config2);
        
        sender_mbx.start();
        receiver_mbx.start();
        
        // Send a message (TiMS assigns header timestamp)
        SensorData sensor{42, 25.5f};
        auto send_result = sender_mbx.send(sensor, 102);
        
        if (!send_result) {
            std::cerr << "  ✗ FAIL: Send failed\n";
            return 1;
        }
        
        // Receive should store in history with TiMS header timestamp
        auto recv_result = receiver_mbx.receive<SensorData>();
        
        if (!recv_result) {
            std::cerr << "  ✗ FAIL: Receive failed\n";
            return 1;
        }
        
        // Verify received data (TimsMessage.header.timestamp from TimsHeader)
        if (recv_result.value().payload.sensor_id != 42) {
            std::cerr << "  ✗ FAIL: Received data mismatch\n";
            return 1;
        }
        
        // Verify stored in history - use TimsMessage.header.timestamp (from TimsHeader)
        uint64_t tims_timestamp = recv_result.value().header.timestamp;
        std::cout << "  TiMS assigned timestamp: " << tims_timestamp << " ns\n";
        
        auto history_result = receiver_mbx.getData<SensorData>(tims_timestamp);
        
        if (!history_result) {
            std::cerr << "  ✗ FAIL: getData failed - message not in history\n";
            return 1;
        }
        
        if (history_result->payload.sensor_id != 42 || history_result->payload.value != 25.5f) {
            std::cerr << "  ✗ FAIL: History data mismatch\n";
            return 1;
        }
        
        std::cout << "  ✓ Receive automatically stores with TimsHeader.timestamp\n";
        
        sender_mbx.stop();
        receiver_mbx.stop();
    }
    
    // Test 3: getData with NEAREST mode (using TiMS timestamps)
    std::cout << "\nTest 3: getData with multiple messages (NEAREST, TiMS timestamps)\n";
    {
        MailboxConfig config1{
            .mailbox_id = 201,
            .max_message_size = 1024
        };
        
        MailboxConfig config2{
            .mailbox_id = 202,
            .max_message_size = 1024
        };
        
        HistoricalMailbox<TestRegistry, 100> producer(config1);
        HistoricalMailbox<TestRegistry, 100> consumer(config2);
        
        producer.start();
        consumer.start();
        
        // Send messages with time spacing (TiMS assigns timestamps)
        for (int i = 0; i < 5; ++i) {
            SensorData sensor{static_cast<uint32_t>(i), static_cast<float>(i * 10.0f)};
            producer.send(sensor, 202);
            Time::sleep(Milliseconds(100));  // 100ms spacing for distinct timestamps
        }
        
        // Receive all messages to populate history and capture TiMS timestamps
        std::vector<uint64_t> tims_timestamps;
        for (int i = 0; i < 5; ++i) {
            auto result = consumer.receive<SensorData>();
            if (!result) {
                std::cerr << "  ✗ FAIL: Failed to receive message " << i << "\n";
                return 1;
            }
            tims_timestamps.push_back(result->header.timestamp);
        }
        
        std::cout << "  Captured TiMS timestamps (ns): ";
        for (auto ts : tims_timestamps) std::cout << ts << " ";
        std::cout << "\n";
        
        // Check timestamp range in buffer
        auto range = consumer.getTimestampRange<SensorData>();
        std::cout << "  Timestamp range: [" << range.first << ", " << range.second << "]\n";
        
        // Test getData with timestamps between messages
        uint64_t midpoint1 = (tims_timestamps[0] + tims_timestamps[1]) / 2;
        auto result1 = consumer.getData<SensorData>(midpoint1);
        if (!result1) {
            std::cerr << "  ✗ FAIL: getData(midpoint=" << midpoint1 << ") returned nullopt\n";
            return 1;
        }
        
        // Verify it's one of the two nearest messages
        if (result1->header.timestamp != tims_timestamps[0] && 
            result1->header.timestamp != tims_timestamps[1]) {
            std::cerr << "  ✗ FAIL: getData returned wrong message (timestamp=" 
                      << result1->header.timestamp << ")\n";
            return 1;
        }
        
        // Query for midpoint between last two messages
        uint64_t midpoint2 = (tims_timestamps[3] + tims_timestamps[4]) / 2;
        auto result2 = consumer.getData<SensorData>(midpoint2);
        if (!result2) {
            std::cerr << "  ✗ FAIL: getData(midpoint2=" << midpoint2 << ") returned nullopt\n";
            return 1;
        }
        
        if (result2->header.timestamp != tims_timestamps[3] && 
            result2->header.timestamp != tims_timestamps[4]) {
            std::cerr << "  ✗ FAIL: getData returned wrong message\n";
            return 1;
        }
        
        std::cout << "  ✓ getData NEAREST mode working with TiMS timestamps\n";
        
        producer.stop();
        consumer.stop();
    }
    
    // Test 4: getData tolerance handling
    std::cout << "\nTest 4: Tolerance enforcement\n";
    {
        MailboxConfig config1{
            .mailbox_id = 301,
            .max_message_size = 1024
        };
        
        MailboxConfig config2{
            .mailbox_id = 302,
            .max_message_size = 1024
        };
        
        HistoricalMailbox<TestRegistry, 100> source(config1, Milliseconds(20));  // 20ms tolerance
        HistoricalMailbox<TestRegistry, 100> sink(config2, Milliseconds(20));
        
        source.start();
        sink.start();
        
        // Send message (TiMS assigns timestamp)
        SensorData sensor{1, 100.0f};
        source.send(sensor, 302);
        
        auto recv_result = sink.receive<SensorData>();
        if (!recv_result) {
            std::cerr << "  ✗ FAIL: Failed to receive\n";
            return 1;
        }
        uint64_t msg_timestamp = recv_result->header.timestamp;
        
        // Within tolerance (10ms away, tolerance is 20ms)
        auto within = sink.getData<SensorData>(msg_timestamp + 10'000'000);  // +10ms
        if (!within) {
            std::cerr << "  ✗ FAIL: Should find message within tolerance\n";
            return 1;
        }
        
        // Outside tolerance (30ms away, tolerance is 20ms)
        auto outside = sink.getData<SensorData>(msg_timestamp + 30'000'000);  // +30ms
        if (outside) {
            std::cerr << "  ✗ FAIL: Should NOT find message outside tolerance\n";
            return 1;
        }
        
        // Override tolerance (allow 50ms)
        auto override_tolerance = sink.getData<SensorData>(msg_timestamp + 30'000'000, Milliseconds(50));
        if (!override_tolerance) {
            std::cerr << "  ✗ FAIL: Should find message with overridden tolerance\n";
            return 1;
        }
        
        std::cout << "  ✓ Tolerance correctly enforced\n";
        
        source.stop();
        sink.stop();
    }
    
    // Test 5: Multiple message types
    std::cout << "\nTest 5: Multiple message types in same mailbox\n";
    {
        MailboxConfig config1{
            .mailbox_id = 401,
            .max_message_size = 1024
        };
        
        MailboxConfig config2{
            .mailbox_id = 402,
            .max_message_size = 1024
        };
        
        HistoricalMailbox<TestRegistry, 100> sender(config1);
        HistoricalMailbox<TestRegistry, 100> receiver(config2);
        
        sender.start();
        receiver.start();
        
        // Send both message types
        SensorData sensor{10, 50.0f};
        ControlData control{75.0f};
        
        sender.send(sensor, 402);
        sender.send(control, 402);
        
        // Receive both
        auto sensor_recv = receiver.receive<SensorData>();
        auto control_recv = receiver.receive<ControlData>();
        
        if (!sensor_recv || !control_recv) {
            std::cerr << "  ✗ FAIL: Failed to receive messages\n";
            return 1;
        }
        
        // getData for each type using their TiMS timestamps
        auto sensor_get = receiver.getData<SensorData>(sensor_recv->header.timestamp);
        auto control_get = receiver.getData<ControlData>(control_recv->header.timestamp);
        
        if (!sensor_get || !control_get) {
            std::cerr << "  ✗ FAIL: getData failed for one of the types\n";
            return 1;
        }
        
        if (sensor_get->payload.sensor_id != 10 || sensor_get->payload.value != 50.0f) {
            std::cerr << "  ✗ FAIL: SensorData mismatch\n";
            return 1;
        }
        
        if (control_get->payload.setpoint != 75.0f) {
            std::cerr << "  ✗ FAIL: ControlData mismatch\n";
            return 1;
        }
        
        std::cout << "  ✓ Multiple message types handled correctly\n";
        
        sender.stop();
        receiver.stop();
    }
    
    // Small delay to let TIMS clean up resources
    Time::sleep(Milliseconds(100));
    
    // Test 6: Timestamp range queries
    std::cout << "\nTest 6: Timestamp range tracking\n";
    {
        MailboxConfig config1{
            .mailbox_id = 501,
            .max_message_size = 1024
        };
        
        MailboxConfig config2{
            .mailbox_id = 502,
            .max_message_size = 1024
        };
        
        HistoricalMailbox<TestRegistry, 100> sender(config1);
        HistoricalMailbox<TestRegistry, 100> receiver(config2);
        
        sender.start();
        receiver.start();
        
        // Empty buffer
        auto [old1, new1] = receiver.getTimestampRange<SensorData>();
        if (old1 != 0 || new1 != 0) {
            std::cerr << "  ✗ FAIL: Empty buffer should return {0, 0}\n";
            return 1;
        }
        
        // Send and receive 3 messages to populate history
        std::vector<uint64_t> timestamps;
        for (int i = 0; i < 3; ++i) {
            SensorData sensor{static_cast<uint32_t>(i), static_cast<float>(i)};
            sender.send(sensor, 502);
            Time::sleep(Milliseconds(10));  // Ensure distinct timestamps
        }
        
        for (int i = 0; i < 3; ++i) {
            auto result = receiver.receive<SensorData>();
            if (!result) {
                std::cerr << "  ✗ FAIL: Failed to receive message " << i << "\n";
                return 1;
            }
            timestamps.push_back(result->header.timestamp);
        }
        
        // Check range matches actual received TiMS timestamps
        auto [oldest, newest] = receiver.getTimestampRange<SensorData>();
        if (oldest != timestamps[0] || newest != timestamps[2]) {
            std::cerr << "  ✗ FAIL: Range should be [" << timestamps[0] << ", " << timestamps[2] 
                      << "], got [" << oldest << ", " << newest << "]\n";
            return 1;
        }
        
        std::cout << "  ✓ Timestamp range tracking working\n";
        
        sender.stop();
        receiver.stop();
    }
    
    std::cout << "\n=== All Phase 6.3/6.10 Tests Passed! ===\n\n";
    std::cout << "HistoricalMailbox validated:\n";
    std::cout << "  ✓ Automatic history storage on receive\n";
    std::cout << "  ✓ TimsHeader.timestamp used (NO payload timestamps)\n";
    std::cout << "  ✓ getData with NEAREST interpolation\n";
    std::cout << "  ✓ Tolerance-based matching\n";
    std::cout << "  ✓ Multiple message types per mailbox\n";
    std::cout << "  ✓ Timestamp range tracking\n\n";
    
    return 0;
}
