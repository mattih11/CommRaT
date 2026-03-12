/**
 * @file test_registry_utils_simple.cpp
 * @brief Simple validation of registry_utils.hpp - no hard expectations
 */

#include <commrat/commrat.hpp>
#include <commrat/messaging/registry_utils.hpp>
#include <iostream>

using namespace commrat;

// Simple test messages
struct TempData { float value; };
struct PressData { float value; };

using MyApp = CommRaT<
    Message::Data<TempData>,
    Message::Data<PressData>
>;

int main() {
    std::cout << "\n==============================================\n";
    std::cout << "  Registry Utilities Validation\n";
    std::cout << "==============================================\n\n";
    
    // Total messages
    std::cout << "Total messages in registry: " << MyApp::size() << "\n\n";
    
    // Category filters
    using DataMsgs = registry::data_messages_t<MyApp>;
    using CmdMsgs = registry::command_messages_t<MyApp>;
    using SubMsgs = registry::subscription_messages_t<MyApp>;
    
    std::cout << "Category Filters:\n";
    std::cout << "  Data messages (UserDefined::Data):     " << registry::tuple_size_v<DataMsgs> << "\n";
    std::cout << "  Command messages (UserDefined::Cmd):   " << registry::tuple_size_v<CmdMsgs> << "\n";
    std::cout << "  Subscription messages (System::Sub):   " << registry::tuple_size_v<SubMsgs> << "\n\n";
    
    // Prefix filters
    using UserMsgs = registry::filter_by_prefix_t<MessagePrefix::UserDefined, MyApp>;
    using SysMsgs = registry::filter_by_prefix_t<MessagePrefix::System, MyApp>;
    
    std::cout << "Prefix Filters:\n";
    std::cout << "  UserDefined prefix:  " << registry::tuple_size_v<UserMsgs> << "\n";
    std::cout << "  System prefix:       " << registry::tuple_size_v<SysMsgs> << "\n\n";
    
    // Request/Reply filters
    using Requests = registry::filter_requests_t<MyApp>;
    using Replies = registry::filter_replies_t<MyApp>;
    
    std::cout << "Request/Reply Filters:\n";
    std::cout << "  Request messages:  " << registry::tuple_size_v<Requests> << "\n";
    std::cout << "  Reply messages:    " << registry::tuple_size_v<Replies> << "\n";
    std::cout << "  Symmetry check:    " << (registry::tuple_size_v<Requests> == registry::tuple_size_v<Replies> ? "PASS" : "FAIL") << "\n\n";
    
    // Convenience counters
    std::cout << "Convenience Counters:\n";
    std::cout << "  data_message_count():         " << registry::data_message_count<MyApp>() << "\n";
    std::cout << "  command_message_count():      " << registry::command_message_count<MyApp>() << "\n";
    std::cout << "  subscription_message_count(): " << registry::subscription_message_count<MyApp>() << "\n";
    std::cout << "  request_message_count():      " << registry::request_message_count<MyApp>() << "\n";
    std::cout << "  reply_message_count():        " << registry::reply_message_count<MyApp>() << "\n\n";
    
    // Registry stats
    using Stats = registry::RegistryStats<MyApp>;
    std::cout << "RegistryStats:\n";
    std::cout << "  Total:         " << Stats::total_messages << "\n";
    std::cout << "  Data:          " << Stats::data_messages << "\n";
    std::cout << "  Commands:      " << Stats::command_messages << "\n";
    std::cout << "  Subscriptions: " << Stats::subscription_messages << "\n";
    std::cout << "  Requests:      " << Stats::request_messages << "\n";
    std::cout << "  Replies:       " << Stats::reply_messages << "\n\n";
    
    // Payload extraction
    using AllPayloads = registry::get_all_payloads_t<MyApp>;
    std::cout << "Payload Extraction:\n";
    std::cout << "  Total payload types: " << registry::tuple_size_v<AllPayloads> << "\n\n";
    
    std::cout << "==============================================\n";
    std::cout << "  VALIDATION COMPLETE\n";
    std::cout << "==============================================\n\n";
    
    return 0;
}
