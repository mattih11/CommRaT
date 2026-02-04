/**
 * @file test_nonblocking.cpp
 * @brief Test to verify try_receive() is truly non-blocking
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <chrono>

using namespace commrat;
using namespace std::chrono;

int main() {
    std::cout << "Testing non-blocking receive...\n";
    
    MailboxConfig config{
        .mailbox_id = 999,
        .mailbox_name = "test_nonblock"
    };
    
    Mailbox<StatusMessage> mailbox(config);
    
    if (auto result = mailbox.start(); !result) {
        std::cerr << "Failed to start mailbox\n";
        return 1;
    }
    
    std::cout << "Mailbox started. Testing try_receive() with empty queue...\n";
    
    // Measure time for try_receive on empty queue
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < 10; ++i) {
        auto msg = mailbox.try_receive<StatusMessage>();
        if (msg) {
            std::cout << "Unexpected message received!\n";
        }
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "10 non-blocking receives took: " << duration.count() << "ms\n";
    
    if (duration.count() < 100) {
        std::cout << "✓ PASS: Non-blocking receive is fast (< 100ms for 10 calls)\n";
    } else {
        std::cout << "✗ FAIL: Non-blocking receive is too slow (>= 100ms for 10 calls)\n";
        return 1;
    }
    
    // Now test that blocking receive actually blocks
    // Note: TiMS adds ~1 second to all non-zero timeouts due to implementation
    std::cout << "\nTesting receive_for() with 200ms timeout...\n";
    start = high_resolution_clock::now();
    
    [[maybe_unused]] auto result = mailbox.receive_for<StatusMessage>(milliseconds(200));
    
    end = high_resolution_clock::now();
    duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "Blocking receive with 200ms timeout took: " << duration.count() << "ms\n";
    
    // TiMS implementation adds 1 second to timeouts, so expect ~1000ms
    if (duration.count() >= 900 && duration.count() <= 1200) {
        std::cout << "✓ PASS: Blocking receive properly times out (~1s due to TiMS)\n";
    } else {
        std::cout << "✗ FAIL: Blocking receive timeout behavior unexpected\n";
        std::cout << "  Expected: ~1000ms, Got: " << duration.count() << "ms\n";
        return 1;
    }
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
