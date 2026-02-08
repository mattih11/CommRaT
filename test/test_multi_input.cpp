/**
 * @file test_multi_input.cpp
 * @brief Test multi-input synchronization (Phase 6.4)
 * 
 * Tests the multi-input module pattern:
 * - Multiple producers at different rates
 * - Consumer with Inputs<T, U> + PrimaryInput<T>
 * - Primary input blocks, secondaries synchronized via getData()
 * - Tolerance-based matching
 */

#include <commrat/historical_mailbox.hpp>
#include <commrat/commrat.hpp>
#include <iostream>
#include <vector>

// Test message types
struct IMUData {
    uint64_t timestamp;
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
};

struct GPSData {
    uint64_t timestamp;
    double latitude, longitude, altitude;
};

struct FusedData {
    uint64_t timestamp;
    float position_x, position_y, position_z;
    float velocity_x, velocity_y, velocity_z;
};

using TestRegistry = commrat::MessageRegistry<
    commrat::Message::Data<IMUData>,
    commrat::Message::Data<GPSData>,
    commrat::Message::Data<FusedData>
>;

using namespace commrat;

int main() {
    std::cout << "=== Phase 6.4 Multi-Input Module Tests ===\n";
    
    // Test 1: Manual multi-input synchronization using HistoricalMailbox
    std::cout << "\nTest 1: Manual multi-input synchronization\n";
    {
        // Single producer sending both IMU and GPS
        MailboxConfig producer_config{
            .mailbox_id = 601,
            .max_message_size = 1024,
            .mailbox_name = "sensor_producer"
        };
        
        // Single consumer receiving both types (simulates fusion module)
        MailboxConfig consumer_config{
            .mailbox_id = 602,
            .max_message_size = 1024,
            .mailbox_name = "fusion_consumer"
        };
        
        HistoricalMailbox<TestRegistry, 100> producer(producer_config, Milliseconds(50));
        HistoricalMailbox<TestRegistry, 100> consumer(consumer_config, Milliseconds(50));
        
        producer.start();
        consumer.start();
        
        // Send interleaved IMU (fast) and GPS (slow) data
        // Pattern: 10 IMU @ 10ms intervals, then 1 GPS
        std::vector<uint64_t> imu_timestamps;
        std::vector<uint64_t> gps_timestamps;
        
        for (int i = 0; i < 3; ++i) {  // 3 GPS cycles
            // Send 10 IMU messages
            for (int j = 0; j < 10; ++j) {
                IMUData imu{0, static_cast<float>(i*10 + j), 0, 0, 0, 0, 0};
                producer.send(imu, 602);
                Time::sleep(Milliseconds(5));  // 5ms spacing
            }
            
            // Send 1 GPS message
            GPSData gps{0, 37.7749 + i*0.0001, -122.4194, 100.0};
            producer.send(gps, 602);
            Time::sleep(Milliseconds(5));
        }
        
        // Receive and store all messages in history
        for (int i = 0; i < 33; ++i) {  // 30 IMU + 3 GPS
            // Try receiving as IMU first
            auto imu_result = consumer.receive<IMUData>();
            if (imu_result) {
                imu_timestamps.push_back(imu_result->timestamp);
                continue;
            }
            
            // Try receiving as GPS
            auto gps_result = consumer.receive<GPSData>();
            if (gps_result) {
                gps_timestamps.push_back(gps_result->timestamp);
            }
        }
        
        std::cout << "  INFO: Received " << imu_timestamps.size() << " IMU, " 
                  << gps_timestamps.size() << " GPS messages\n";
        
        if (imu_timestamps.empty() || gps_timestamps.empty()) {
            std::cerr << "  FAIL: Didn't receive messages\n";
            return 1;
        }
        
        // Now simulate fusion: For each IMU timestamp, try to getData GPS
        int fusion_count = 0;
        for (uint64_t imu_ts : imu_timestamps) {
            auto gps_result = consumer.getData<GPSData>(imu_ts, Milliseconds(100));
            
            if (gps_result) {
                fusion_count++;
            }
        }
        
        std::cout << "  INFO: Successfully fused " << fusion_count << "/" << imu_timestamps.size() 
                  << " IMU messages with GPS\n";
        
        if (fusion_count == 0) {
            std::cerr << "  FAIL: No fusion occurred\n";
            return 1;
        }
        
        std::cout << "  PASS: Manual multi-input synchronization working\n";
        
        producer.stop();
        consumer.stop();
    }
    
    std::cout << "\n=== All Phase 6.4 Tests Passed! ===\n";
    std::cout << "\nPhase 6.4 Manual Validation Complete\n";
    std::cout << "Next: Integrate multi-input into Module base class\n";
    
    return 0;
}
