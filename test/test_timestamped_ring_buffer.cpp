/**
 * @file test_timestamped_ring_buffer.cpp
 * @brief Test Phase 6.2 OutputBuffer functionality
 * 
 * Validates:
 * - Thread-safe push/get_data operations
 * - Timestamp-based lookup (NEAREST/BEFORE/AFTER)
 * - Tolerance handling
 * - Buffer overflow behavior
 */

#include "commrat/mailbox/timestamped_ring_buffer.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>

// Test message type with timestamp
struct TestMessage {
    uint64_t timestamp;
    int value;
    float data;
};

using namespace commrat;

int main() {
    std::cout << "=== Phase 6.2 OutputBuffer Tests ===\n\n";

    // Test 1: Basic push and get_data (NEAREST mode)
    {
        std::cout << "Test 1: Basic push and get_data (NEAREST)\n";
        
        OutputBuffer<TestMessage, 10> buffer;
        
        // Use nanosecond timestamps
        uint64_t t1 = 1000000000ULL;  // 1 second
        uint64_t t2 = 1100000000ULL;  // 1.1 seconds
        uint64_t t3 = 1200000000ULL;  // 1.2 seconds
        
        buffer.push(TestMessage{.timestamp = t1, .value = 1, .data = 1.0f});
        buffer.push(TestMessage{.timestamp = t2, .value = 2, .data = 2.0f});
        buffer.push(TestMessage{.timestamp = t3, .value = 3, .data = 3.0f});
        
        assert(buffer.size() == 3);
        
        // Exact match
        auto result = buffer.get_data(t2);
        assert(result);
        assert(result->timestamp == t2);
        assert(result->value == 2);
        
        // Nearest (closer to t2) - 20ms from t2
        result = buffer.get_data(t2 + 20'000'000);
        assert(result);
        assert(result->timestamp == t2);
        
        // Nearest (closer to t3) - 80ms from t2, 20ms from t3
        result = buffer.get_data(t2 + 80'000'000);
        assert(result);
        assert(result->timestamp == t3);
        
        std::cout << "  PASS: Basic get_data working correctly\n\n";
    }

    // Test 2: BEFORE mode
    {
        std::cout << "Test 2: get_data BEFORE mode\n";
        
        OutputBuffer<TestMessage, 10> buffer;
        
        uint64_t t1 = 1000000000ULL;
        uint64_t t2 = 1100000000ULL;
        uint64_t t3 = 1200000000ULL;
        
        buffer.push(TestMessage{.timestamp = t1, .value = 1, .data = 1.0f});
        buffer.push(TestMessage{.timestamp = t2, .value = 2, .data = 2.0f});
        buffer.push(TestMessage{.timestamp = t3, .value = 3, .data = 3.0f});
        
        // Get message at or before t2+50ms
        auto result = buffer.get_data(t2 + 50'000'000, std::chrono::milliseconds(100), 
                                     InterpolationMode::BEFORE);
        assert(result);
        assert(result->timestamp == t2);  // Newest before query
        
        // Get message at or before t3+50ms
        result = buffer.get_data(t3 + 50'000'000, std::chrono::milliseconds(100),
                               InterpolationMode::BEFORE);
        assert(result);
        assert(result->timestamp == t3);  // Newest before query
        
        // Too far before (200ms before t1, only 50ms tolerance)
        result = buffer.get_data(t1 - 200'000'000, std::chrono::milliseconds(50),
                               InterpolationMode::BEFORE);
        assert(!result);  // No message within tolerance
        
        std::cout << "  PASS: BEFORE mode working correctly\n\n";
    }

    // Test 3: AFTER mode
    {
        std::cout << "Test 3: get_data AFTER mode\n";
        
        OutputBuffer<TestMessage, 10> buffer;
        
        uint64_t t1 = 1000000000ULL;
        uint64_t t2 = 1100000000ULL;
        uint64_t t3 = 1200000000ULL;
        
        buffer.push(TestMessage{.timestamp = t1, .value = 1, .data = 1.0f});
        buffer.push(TestMessage{.timestamp = t2, .value = 2, .data = 2.0f});
        buffer.push(TestMessage{.timestamp = t3, .value = 3, .data = 3.0f});
        
        // Get message at or after t1+50ms
        auto result = buffer.get_data(t1 + 50'000'000, std::chrono::milliseconds(100),
                                     InterpolationMode::AFTER);
        assert(result);
        assert(result->timestamp == t2);  // Oldest after query
        
        // Get message at or after t1-50ms
        result = buffer.get_data(t1 - 50'000'000, std::chrono::milliseconds(100),
                               InterpolationMode::AFTER);
        assert(result);
        assert(result->timestamp == t1);  // Oldest after query
        
        // Too far after (200ms after t3, only 50ms tolerance)
        result = buffer.get_data(t3 + 200'000'000, std::chrono::milliseconds(50),
                               InterpolationMode::AFTER);
        assert(!result);  // No message within tolerance
        
        std::cout << "  PASS: AFTER mode working correctly\n\n";
    }

    // Test 4: Tolerance behavior (using nanosecond timestamps)
    {
        std::cout << "Test 4: Tolerance handling\n";
        
        OutputBuffer<TestMessage, 10> buffer(std::chrono::milliseconds(30));
        
        // Use nanosecond timestamps (as expected by get_data implementation)
        uint64_t base_ns = 1000000000ULL;  // 1 second in nanoseconds
        buffer.push(TestMessage{.timestamp = base_ns, .value = 1, .data = 1.0f});
        buffer.push(TestMessage{.timestamp = base_ns + 100'000'000, .value = 2, .data = 2.0f});  // +100ms
        
        // Within tolerance (25ms from base, 30ms tolerance)
        auto result = buffer.get_data(base_ns + 25'000'000);
        assert(result);
        assert(result->timestamp == base_ns);
        
        // Outside tolerance (50ms from both messages, 30ms tolerance)
        result = buffer.get_data(base_ns + 50'000'000);
        assert(!result);
        
        // Override tolerance (50ms within 100ms tolerance)
        result = buffer.get_data(base_ns + 50'000'000, std::chrono::milliseconds(100));
        assert(result);  // Now within 100ms tolerance
        
        std::cout << "  PASS: Tolerance correctly enforced\n\n";
    }

    // Test 5: Buffer overflow (FIFO overwrite)
    {
        std::cout << "Test 5: Buffer overflow behavior\n";
        
        OutputBuffer<TestMessage, 5> buffer;
        
        // Fill buffer
        for (int i = 0; i < 5; ++i) {
            buffer.push(TestMessage{.timestamp = static_cast<uint64_t>(1000 + i * 100), 
                                    .value = i, 
                                    .data = static_cast<float>(i)});
        }
        
        assert(buffer.size() == 5);
        assert(buffer.full());
        
        auto range = buffer.get_timestamp_range();
        assert(range.first == 1000);
        assert(range.second == 1400);
        
        // Overflow - should overwrite oldest
        buffer.push(TestMessage{.timestamp = 1500, .value = 5, .data = 5.0f});
        
        assert(buffer.size() == 5);  // Still at capacity
        range = buffer.get_timestamp_range();
        assert(range.first == 1100);  // Oldest is now 1100 (1000 was overwritten)
        assert(range.second == 1500);  // Newest is 1500
        
        // Verify old data no longer accessible
        auto result = buffer.get_data(1000);
        assert(!result || result->timestamp != 1000);
        
        // Verify new data accessible
        result = buffer.get_data(1500);
        assert(result);
        assert(result->timestamp == 1500);
        
        std::cout << "  PASS: FIFO overwrite working correctly\n\n";
    }

    // Test 6: Thread safety (concurrent push/get_data)
    {
        std::cout << "Test 6: Thread safety\n";
        
        OutputBuffer<TestMessage, 100> buffer;
        
        std::atomic<bool> done{false};
        std::atomic<int> successful_reads{0};
        
        // Producer thread
        std::thread producer([&]() {
            for (int i = 0; i < 1000; ++i) {
                buffer.push(TestMessage{.timestamp = static_cast<uint64_t>(1000 + i), 
                                        .value = i, 
                                        .data = static_cast<float>(i)});
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            done = true;
        });
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int c = 0; c < 3; ++c) {
            consumers.emplace_back([&, c]() {
                while (!done) {
                    uint64_t timestamp = 1000 + (c * 100);
                    auto result = buffer.get_data(timestamp, std::chrono::milliseconds(500));
                    if (result) {
                        successful_reads++;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            });
        }
        
        producer.join();
        for (auto& consumer : consumers) {
            consumer.join();
        }
        
        std::cout << "  Successful concurrent reads: " << successful_reads << "\n";
        std::cout << "  Final buffer size: " << buffer.size() << "\n";
        assert(buffer.size() == 100);  // Should be at capacity
        
        std::cout << "  PASS: Thread-safe operations verified\n\n";
    }

    // Test 7: Timestamp range queries
    {
        std::cout << "Test 7: Timestamp range queries\n";
        
        OutputBuffer<TestMessage, 10> buffer;
        
        // Empty buffer
        auto range = buffer.get_timestamp_range();
        assert(range.first == 0);
        assert(range.second == 0);
        
        // Single message
        buffer.push(TestMessage{.timestamp = 1234, .value = 1, .data = 1.0f});
        range = buffer.get_timestamp_range();
        assert(range.first == 1234);
        assert(range.second == 1234);
        
        // Multiple messages
        buffer.push(TestMessage{.timestamp = 1500, .value = 2, .data = 2.0f});
        buffer.push(TestMessage{.timestamp = 1750, .value = 3, .data = 3.0f});
        range = buffer.get_timestamp_range();
        assert(range.first == 1234);
        assert(range.second == 1750);
        
        std::cout << "  Timestamp range: [" << range.first << ", " << range.second << "]\n";
        std::cout << "  PASS: Timestamp range tracking working\n\n";
    }

    std::cout << "=== All Phase 6.2 Tests Passed! ===\n";
    std::cout << "\nPhase 6.2 Complete: OutputBuffer ready\n";
    std::cout << "Features validated:\n";
    std::cout << "  ✓ Thread-safe push/get_data operations\n";
    std::cout << "  ✓ NEAREST/BEFORE/AFTER interpolation modes\n";
    std::cout << "  ✓ Tolerance-based timestamp matching\n";
    std::cout << "  ✓ FIFO overflow behavior\n";
    std::cout << "  ✓ Concurrent read/write safety\n";
    std::cout << "\nNext: Implement HistoricalMailbox wrapper (Phase 6.3)\n";

    return 0;
}
