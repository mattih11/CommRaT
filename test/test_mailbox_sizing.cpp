/**
 * @file test_mailbox_sizing.cpp
 * @brief Test compile-time mailbox buffer sizing with max_size_for_types
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <cassert>
#include <array>

// Test message types with different sizes
struct TinyCmd {
    uint8_t value;  // 1 byte payload
};

struct SmallCmd {
    uint32_t value;  // 4 bytes payload
};

struct MediumData {
    std::array<float, 32> values;  // 128 bytes payload
};

struct LargeData {
    std::array<uint8_t, 2000> buffer;  // 2000 bytes payload
};

// Create registry with all types
using TestRegistry = commrat::MessageRegistry<
    commrat::MessageDefinition<TinyCmd, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Commands>,
    commrat::MessageDefinition<SmallCmd, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Commands>,
    commrat::MessageDefinition<MediumData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<LargeData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>
>;

int main() {
    std::cout << "=== Mailbox Buffer Sizing Test ===\n\n";
    
    // Calculate actual serialized sizes (with TimsHeader)
    constexpr size_t tiny_size = sertial::Message<commrat::TimsMessage<TinyCmd>>::max_buffer_size;
    constexpr size_t small_size = sertial::Message<commrat::TimsMessage<SmallCmd>>::max_buffer_size;
    constexpr size_t medium_size = sertial::Message<commrat::TimsMessage<MediumData>>::max_buffer_size;
    constexpr size_t large_size = sertial::Message<commrat::TimsMessage<LargeData>>::max_buffer_size;
    
    std::cout << "Individual message sizes (with TimsHeader):\n";
    std::cout << "  TinyCmd:    " << tiny_size << " bytes\n";
    std::cout << "  SmallCmd:   " << small_size << " bytes\n";
    std::cout << "  MediumData: " << medium_size << " bytes\n";
    std::cout << "  LargeData:  " << large_size << " bytes\n";
    std::cout << "\n";
    
    // Test max_message_size (should be largest)
    constexpr size_t registry_max = TestRegistry::max_message_size;
    std::cout << "Registry::max_message_size: " << registry_max << " bytes\n";
    assert(registry_max == large_size);
    std::cout << "✓ Correct: matches LargeData size\n\n";
    
    // Test max_size_for_types with command subset
    constexpr size_t cmd_max = TestRegistry::max_size_for_types<TinyCmd, SmallCmd>();
    std::cout << "max_size_for_types<TinyCmd, SmallCmd>: " << cmd_max << " bytes\n";
    assert(cmd_max == small_size);
    assert(cmd_max < medium_size);
    assert(cmd_max < large_size);
    std::cout << "✓ Correct: matches SmallCmd size (max of the two)\n";
    std::cout << "  Memory savings vs registry_max: " 
              << (registry_max - cmd_max) << " bytes ("
              << (100.0 * (registry_max - cmd_max) / registry_max) << "%)\n\n";
    
    // Test max_size_for_types with single type
    constexpr size_t tiny_only = TestRegistry::max_size_for_types<TinyCmd>();
    std::cout << "max_size_for_types<TinyCmd>: " << tiny_only << " bytes\n";
    assert(tiny_only == tiny_size);
    std::cout << "✓ Correct: matches TinyCmd size\n";
    std::cout << "  Memory savings vs registry_max: " 
              << (registry_max - tiny_only) << " bytes ("
              << (100.0 * (registry_max - tiny_only) / registry_max) << "%)\n\n";
    
    // Test max_size_for_types with data subset
    constexpr size_t data_max = TestRegistry::max_size_for_types<MediumData>();
    std::cout << "max_size_for_types<MediumData>: " << data_max << " bytes\n";
    assert(data_max == medium_size);
    assert(data_max < large_size);
    std::cout << "✓ Correct: matches MediumData size\n";
    std::cout << "  Memory savings vs registry_max: " 
              << (registry_max - data_max) << " bytes ("
              << (100.0 * (registry_max - data_max) / registry_max) << "%)\n\n";
    
    // Calculate potential savings in a module
    std::cout << "=== Module Mailbox Memory Analysis ===\n\n";
    
    constexpr size_t slots = 10;
    
    // OLD approach: all mailboxes use registry_max
    size_t old_cmd_buffer = registry_max * slots;
    size_t old_work_buffer = registry_max * slots;
    size_t old_data_buffer = registry_max * slots;
    size_t old_total = old_cmd_buffer + old_work_buffer + old_data_buffer;
    
    std::cout << "OLD (all use registry_max = " << registry_max << " bytes):\n";
    std::cout << "  CMD  mailbox: " << old_cmd_buffer << " bytes (" << slots << " slots)\n";
    std::cout << "  WORK mailbox: " << old_work_buffer << " bytes (" << slots << " slots)\n";
    std::cout << "  DATA mailbox: " << old_data_buffer << " bytes (" << slots << " slots)\n";
    std::cout << "  TOTAL:        " << old_total << " bytes\n\n";
    
    // NEW approach: typed mailboxes with correct sizes
    // Assume: CMD handles TinyCmd+SmallCmd, WORK handles system messages (~64 bytes), DATA handles MediumData
    constexpr size_t system_max = 64;  // Approximate system message size
    size_t new_cmd_buffer = cmd_max * slots;
    size_t new_work_buffer = system_max * slots;
    size_t new_data_buffer = data_max * slots;
    size_t new_total = new_cmd_buffer + new_work_buffer + new_data_buffer;
    
    std::cout << "NEW (typed mailboxes):\n";
    std::cout << "  CMD  mailbox: " << new_cmd_buffer << " bytes (" << slots << " slots × " << cmd_max << " bytes)\n";
    std::cout << "  WORK mailbox: " << new_work_buffer << " bytes (" << slots << " slots × " << system_max << " bytes)\n";
    std::cout << "  DATA mailbox: " << new_data_buffer << " bytes (" << slots << " slots × " << data_max << " bytes)\n";
    std::cout << "  TOTAL:        " << new_total << " bytes\n\n";
    
    size_t savings = old_total - new_total;
    double percent = 100.0 * savings / old_total;
    
    std::cout << "SAVINGS: " << savings << " bytes (" << percent << "%)\n\n";
    
    std::cout << "✅ All tests passed!\n";
    
    return 0;
}
