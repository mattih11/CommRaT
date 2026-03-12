/**
 * @file test_registry_utils.cpp
 * @brief Test registry_utils.hpp compile-time query helpers
 */

#include <commrat/commrat.hpp>
#include <commrat/messaging/registry_utils.hpp>
#include <iostream>
#include <cassert>

using namespace commrat;

// ============================================================================
// Test Message Definitions
// ============================================================================

// Data messages
struct TemperatureData {
    float value;
    uint64_t timestamp;
};

struct PressureData {
    float value;
    uint64_t timestamp;
};

// Command messages
struct CalibrateCmdPayload {
    uint8_t sensor_id;
};
struct CalibrateReplyPayload {
    bool success;
};

struct ResetCmdPayload {
    uint32_t flags;
};
struct ResetReplyPayload {
    bool success;
};

// Using CommRaT API
using MyApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<PressureData>,
    Message::Command<CalibrateCmdPayload, CalibrateReplyPayload>,
    Message::Command<ResetCmdPayload, ResetReplyPayload>
>;

// ============================================================================
// Test Compile-Time Filters
// ============================================================================

void test_category_filters() {
    std::cout << "Testing category filters...\n";
    
    // Test data messages filter
    using DataMsgs = registry::data_messages_t<MyApp>;
    constexpr size_t num_data = registry::tuple_size_v<DataMsgs>;
    static_assert(num_data == 2, "Should have 2 data messages");
    std::cout << "  Data messages: " << num_data << " (expected 2)\n";
    
    // Test command messages filter
    using CommandMsgs = registry::command_messages_t<MyApp>;
    constexpr size_t num_commands = registry::tuple_size_v<CommandMsgs>;
    static_assert(num_commands == 4, "Should have 4 command messages (2 request + 2 reply)");
    std::cout << "  Command messages: " << num_commands << " (expected 4)\n";
    
    // Test subscription messages filter
    using SubMsgs = registry::subscription_messages_t<MyApp>;
    constexpr size_t num_sub = registry::tuple_size_v<SubMsgs>;
    static_assert(num_sub == 4, "Should have 4 subscription messages (Subscribe req/reply, Unsubscribe req/reply)");
    std::cout << "  Subscription messages: " << num_sub << " (expected 4)\n";
    
    std::cout << "  PASS: Category filters\n\n";
}

void test_prefix_filters() {
    std::cout << "Testing prefix filters...\n";
    
    // Filter by UserDefined prefix
    using UserMsgs = registry::filter_by_prefix_t<MessagePrefix::UserDefined, MyApp>;
    constexpr size_t num_user = registry::tuple_size_v<UserMsgs>;
    std::cout << "  UserDefined messages: " << num_user << "\n";
    
    // Filter by System prefix
    using SystemMsgs = registry::filter_by_prefix_t<MessagePrefix::System, MyApp>;
    constexpr size_t num_system = registry::tuple_size_v<SystemMsgs>;
    std::cout << "  System messages: " << num_system << "\n";
    
    // Validate counts
    static_assert(num_user > 0, "Should have user-defined messages");
    static_assert(num_system > 0, "Should have system messages");
    
    std::cout << "  PASS: Prefix filters\n\n";
}

void test_subprefix_filters() {
    std::cout << "Testing subprefix filters...\n";
    
    // Filter by UserDefined::Data
    using UserDataMsgs = registry::filter_by_subprefix_t<
        MessagePrefix::UserDefined,
        static_cast<uint8_t>(UserSubPrefix::Data),
        MyApp
    >;
    constexpr size_t num_user_data = registry::tuple_size_v<UserDataMsgs>;
    static_assert(num_user_data == 2, "Should have exactly 2 user data messages");
    std::cout << "  UserDefined::Data: " << num_user_data << " (expected 2)\n";
    
    // Filter by UserDefined::Commands
    using UserCmdMsgs = registry::filter_by_subprefix_t<
        MessagePrefix::UserDefined,
        static_cast<uint8_t>(UserSubPrefix::Commands),
        MyApp
    >;
    constexpr size_t num_user_cmd = registry::tuple_size_v<UserCmdMsgs>;
    static_assert(num_user_cmd == 4, "Should have 4 user command messages");
    std::cout << "  UserDefined::Commands: " << num_user_cmd << " (expected 4)\n";
    
    std::cout << "  PASS: Subprefix filters\n\n";
}

void test_request_reply_filters() {
    std::cout << "Testing request/reply filters...\n";
    
    // Filter requests
    using Requests = registry::filter_requests_t<MyApp>;
    constexpr size_t num_requests = registry::tuple_size_v<Requests>;
    std::cout << "  Request messages: " << num_requests << "\n";
    
    // Filter replies
    using Replies = registry::filter_replies_t<MyApp>;
    constexpr size_t num_replies = registry::tuple_size_v<Replies>;
    std::cout << "  Reply messages: " << num_replies << "\n";
    
    // Validate symmetry
    static_assert(num_requests == num_replies, "Should have equal requests and replies");
    std::cout << "  Request/reply symmetry validated\n";
    
    std::cout << "  PASS: Request/reply filters\n\n";
}

void test_convenience_counters() {
    std::cout << "Testing convenience counter functions...\n";
    
    constexpr size_t data_count = registry::data_message_count<MyApp>();
    constexpr size_t cmd_count = registry::command_message_count<MyApp>();
    constexpr size_t sub_count = registry::subscription_message_count<MyApp>();
    constexpr size_t req_count = registry::request_message_count<MyApp>();
    constexpr size_t reply_count = registry::reply_message_count<MyApp>();
    
    std::cout << "  Data messages:         " << data_count << "\n";
    std::cout << "  Command messages:      " << cmd_count << "\n";
    std::cout << "  Subscription messages: " << sub_count << "\n";
    std::cout << "  Request messages:      " << req_count << "\n";
    std::cout << "  Reply messages:        " << reply_count << "\n";
    
    static_assert(data_count == 2, "Data count mismatch");
    static_assert(cmd_count == 4, "Command count mismatch");
    static_assert(sub_count == 4, "Subscription count mismatch");
    static_assert(req_count == reply_count, "Request/reply count mismatch");
    
    std::cout << "  PASS: Convenience counters\n\n";
}

void test_registry_stats() {
    std::cout << "Testing RegistryStats introspection...\n";
    
    using Stats = registry::RegistryStats<MyApp>;
    
    std::cout << "  Total messages:        " << Stats::total_messages << "\n";
    std::cout << "  Data messages:         " << Stats::data_messages << "\n";
    std::cout << "  Command messages:      " << Stats::command_messages << "\n";
    std::cout << "  Subscription messages: " << Stats::subscription_messages << "\n";
    std::cout << "  Request messages:      " << Stats::request_messages << "\n";
    std::cout << "  Reply messages:        " << Stats::reply_messages << "\n";
    
    // Validation constraints are built into RegistryStats as static_asserts
    std::cout << "  PASS: RegistryStats\n\n";
}

void test_payload_extraction() {
    std::cout << "Testing payload extraction...\n";
    
    // Extract all payloads
    using AllPayloads = registry::get_all_payloads_t<MyApp>;
    constexpr size_t num_payloads = registry::tuple_size_v<AllPayloads>;
    std::cout << "  Total payload types: " << num_payloads << "\n";
    
    // Extract payloads from filtered messages
    using DataMsgs = registry::data_messages_t<MyApp>;
    using DataPayloads = registry::extract_payloads_t<DataMsgs>;
    constexpr size_t num_data_payloads = registry::tuple_size_v<DataPayloads>;
    static_assert(num_data_payloads == 2, "Should have 2 data payloads");
    std::cout << "  Data payload types:  " << num_data_payloads << " (expected 2)\n";
    
    std::cout << "  PASS: Payload extraction\n\n";
}

void test_tuple_utilities() {
    std::cout << "Testing tuple utilities...\n";
    
    // Test empty tuple check
    using EmptyTuple = std::tuple<>;
    static_assert(registry::is_empty_tuple_v<EmptyTuple>, "Empty tuple check failed");
    
    using NonEmptyTuple = std::tuple<int, float>;
    static_assert(!registry::is_empty_tuple_v<NonEmptyTuple>, "Non-empty tuple check failed");
    
    // Test contains_type
    using TestTuple = std::tuple<int, float, double>;
    static_assert(registry::contains_type_v<int, TestTuple>, "Should contain int");
    static_assert(registry::contains_type_v<float, TestTuple>, "Should contain float");
    static_assert(!registry::contains_type_v<char, TestTuple>, "Should not contain char");
    
    std::cout << "  PASS: Tuple utilities\n\n";
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "   Registry Utilities Test Suite       \n";
    std::cout << "========================================\n\n";
    
    try {
        test_category_filters();
        test_prefix_filters();
        test_subprefix_filters();
        test_request_reply_filters();
        test_convenience_counters();
        test_registry_stats();
        test_payload_extraction();
        test_tuple_utilities();
        
        std::cout << "========================================\n";
        std::cout << "   ALL TESTS PASSED                   \n";
        std::cout << "========================================\n\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
