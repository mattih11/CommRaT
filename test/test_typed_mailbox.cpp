/**
 * @file test_typed_mailbox.cpp
 * @brief Test TypedMailbox type validation and buffer sizing
 */

#include <commrat/commrat.hpp>
#include <iostream>

// Test message types
struct CommandA {
    uint8_t cmd_id;
};

struct CommandB {
    uint32_t value;
};

struct DataMessage {
    std::array<float, 64> data;
};

// Create registry
using TestRegistry = commrat::MessageRegistry<
    commrat::MessageDefinition<CommandA, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Commands>,
    commrat::MessageDefinition<CommandB, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Commands>,
    commrat::MessageDefinition<DataMessage, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>
>;

int main() {
    std::cout << "=== TypedMailbox Compilation Test ===\n\n";
    
    // Calculate sizes
    constexpr size_t registry_max = TestRegistry::max_message_size;
    constexpr size_t cmd_max = TestRegistry::max_size_for_types<CommandA, CommandB>();
    constexpr size_t data_max = TestRegistry::max_size_for_types<DataMessage>();
    
    std::cout << "Registry max_message_size: " << registry_max << " bytes\n";
    std::cout << "Command types max size: " << cmd_max << " bytes\n";
    std::cout << "Data type max size: " << data_max << " bytes\n\n";
    
    // Test TypedMailbox constants
    using CmdMailbox = commrat::TypedMailbox<TestRegistry, CommandA, CommandB>;
    using DataMailbox = commrat::TypedMailbox<TestRegistry, DataMessage>;
    
    std::cout << "CmdMailbox max_message_size: " << CmdMailbox::max_message_size << " bytes\n";
    std::cout << "DataMailbox max_message_size: " << DataMailbox::max_message_size << " bytes\n\n";
    
    // Verify correct sizing
    assert(CmdMailbox::max_message_size == cmd_max);
    assert(DataMailbox::max_message_size == data_max);
    assert(CmdMailbox::max_message_size < registry_max);
    // DataMailbox might equal registry_max if DataMessage is the largest
    assert(DataMailbox::max_message_size <= registry_max);
    
    std::cout << "✓ Buffer sizes correctly optimized\n";
    std::cout << "  CmdMailbox saves: " << (registry_max - cmd_max) << " bytes per slot ("
              << (100.0 * (registry_max - cmd_max) / registry_max) << "%)\n\n";
    
    // Test type checking at compile time
    std::cout << "Type validation (compile-time):\n";
    std::cout << "  CmdMailbox::is_allowed<CommandA>(): " << CmdMailbox::is_allowed<CommandA>() << "\n";
    std::cout << "  CmdMailbox::is_allowed<CommandB>(): " << CmdMailbox::is_allowed<CommandB>() << "\n";
    std::cout << "  CmdMailbox::is_allowed<DataMessage>(): " << CmdMailbox::is_allowed<DataMessage>() << "\n";
    
    assert(CmdMailbox::is_allowed<CommandA>() == true);
    assert(CmdMailbox::is_allowed<CommandB>() == true);
    assert(CmdMailbox::is_allowed<DataMessage>() == false);
    
    std::cout << "✓ Type restrictions working\n\n";
    
    // Test convenience aliases
    using SingleCmd = commrat::SingleTypeMailbox<TestRegistry, CommandA>;
    using CmdMailbox2 = commrat::CommandMailbox<TestRegistry, CommandA, CommandB>;
    using DataMailbox2 = commrat::DataMailbox<TestRegistry, DataMessage>;
    
    std::cout << "Convenience aliases:\n";
    std::cout << "  SingleTypeMailbox: " << SingleCmd::max_message_size << " bytes\n";
    std::cout << "  CommandMailbox: " << CmdMailbox2::max_message_size << " bytes\n";
    std::cout << "  DataMailbox: " << DataMailbox2::max_message_size << " bytes\n";
    
    assert(SingleCmd::max_message_size == TestRegistry::max_size_for_types<CommandA>());
    assert(CmdMailbox2::max_message_size == cmd_max);
    assert(DataMailbox2::max_message_size == data_max);
    
    std::cout << "✓ Convenience aliases working\n\n";
    
    // Note: Actual send/receive tests would require TiMS initialization
    // This test focuses on compile-time features
    
    std::cout << "✅ All compile-time tests passed!\n";
    std::cout << "\nNote: Runtime send/receive tests require TiMS initialization\n";
    std::cout << "      and are covered in integration tests.\n";
    
    return 0;
}

// Uncomment these to test compile-time error messages:
/*
void test_compile_errors() {
    using CmdMailbox = commrat::TypedMailbox<TestRegistry, CommandA, CommandB>;
    commrat::MailboxConfig config{.mailbox_id = 100};
    CmdMailbox cmd_mailbox(config);
    
    // This should fail at compile time with helpful error:
    // "Message type not allowed in this typed mailbox"
    DataMessage data{};
    cmd_mailbox.send(data, 200);  // ERROR: DataMessage not in AllowedPayloadTypes
}
*/
