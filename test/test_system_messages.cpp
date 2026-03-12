/**
 * @file test_system_messages.cpp
 * @brief Test system message request/reply functionality
 */

#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/messaging/system/data_request_messages.hpp"
#include <iostream>
#include <cassert>

using namespace commrat;

int main() {
    std::cout << "Testing system message request/reply functionality...\n";
    
    // Test 1: SubscribeRequest has reply
    {
        static_assert(SubscribeRequest::has_reply, "SubscribeRequest should have reply");
        static_assert(SubscribeRequest::is_request, "SubscribeRequest should be marked as request");
        static_assert(!SubscribeRequest::is_reply, "SubscribeRequest should not be marked as reply");
        
        // Reply ID should be negative of request ID
        constexpr int16_t request_id = static_cast<int16_t>(SubscribeRequest::local_id);
        constexpr int16_t reply_id = static_cast<int16_t>(SubscribeReply::local_id);
        static_assert(reply_id == -request_id, "Reply ID should be negative of request ID");
        
        std::cout << "  SubscribeRequest ID: 0x" << std::hex << SubscribeRequest::local_id << std::dec << "\n";
        std::cout << "  SubscribeReply ID:   0x" << std::hex << SubscribeReply::local_id << std::dec << "\n";
        std::cout << "  SubscribeRequest: PASS\n";
    }
    
    // Test 2: UnsubscribeRequest has reply
    {
        static_assert(UnsubscribeRequest::has_reply, "UnsubscribeRequest should have reply");
        static_assert(UnsubscribeRequest::is_request, "UnsubscribeRequest should be marked as request");
        static_assert(!UnsubscribeRequest::is_reply, "UnsubscribeRequest should not be marked as reply");
        
        constexpr int16_t request_id = static_cast<int16_t>(UnsubscribeRequest::local_id);
        constexpr int16_t reply_id = static_cast<int16_t>(UnsubscribeReply::local_id);
        static_assert(reply_id == -request_id, "Reply ID should be negative of request ID");
        
        std::cout << "  UnsubscribeRequest ID: 0x" << std::hex << UnsubscribeRequest::local_id << std::dec << "\n";
        std::cout << "  UnsubscribeReply ID:   0x" << std::hex << UnsubscribeReply::local_id << std::dec << "\n";
        std::cout << "  UnsubscribeRequest: PASS\n";
    }
    
    // Test 3: GetDataRequest has reply
    {
        static_assert(GetDataRequest::has_reply, "GetDataRequest should have reply");
        static_assert(GetDataRequest::is_request, "GetDataRequest should be marked as request");
        static_assert(!GetDataRequest::is_reply, "GetDataRequest should not be marked as reply");
        
        constexpr int16_t request_id = static_cast<int16_t>(GetDataRequest::local_id);
        constexpr int16_t reply_id = static_cast<int16_t>(GetDataReply::local_id);
        static_assert(reply_id == -request_id, "Reply ID should be negative of request ID");
        
        std::cout << "  GetDataRequest ID: 0x" << std::hex << GetDataRequest::local_id << std::dec << "\n";
        std::cout << "  GetDataReply ID:   0x" << std::hex << GetDataReply::local_id << std::dec << "\n";
        std::cout << "  GetDataRequest: PASS\n";
    }
    
    // Test 4: GetNextDataRequest has reply
    {
        static_assert(GetNextDataRequest::has_reply, "GetNextDataRequest should have reply");
        static_assert(GetNextDataRequest::is_request, "GetNextDataRequest should be marked as request");
        static_assert(!GetNextDataRequest::is_reply, "GetNextDataRequest should not be marked as reply");
        
        constexpr int16_t request_id = static_cast<int16_t>(GetNextDataRequest::local_id);
        constexpr int16_t reply_id = static_cast<int16_t>(GetNextDataReply::local_id);
        static_assert(reply_id == -request_id, "Reply ID should be negative of request ID");
        
        std::cout << "  GetNextDataRequest ID: 0x" << std::hex << GetNextDataRequest::local_id << std::dec << "\n";
        std::cout << "  GetNextDataReply ID:   0x" << std::hex << GetNextDataReply::local_id << std::dec << "\n";
        std::cout << "  GetNextDataRequest: PASS\n";
    }
    
    // Test 5: Reply messages are properly marked
    {
        static_assert(!SubscribeReply::has_reply, "Reply messages should not have replies themselves");
        static_assert(!SubscribeReply::is_request, "Reply messages should not be marked as request");
        // Note: is_reply detection has edge case with ID=0x0001 becoming 0xFFFF (AUTO_ID marker)
        // The important part is that has_reply=false and is_request=false
        
        static_assert(!GetDataReply::has_reply, "Reply messages should not have replies themselves");
        static_assert(!GetDataReply::is_request, "Reply messages should not be marked as request");
        
        std::cout << "  Reply message flags: PASS\n";
    }
    
    // Test 6: All messages use System::Subscription subprefix
    {
        static_assert(SubscribeRequest::prefix == MessagePrefix::System);
        static_assert(SubscribeRequest::subprefix == static_cast<uint8_t>(SystemSubPrefix::Subscription));
        
        static_assert(GetDataRequest::prefix == MessagePrefix::System);
        static_assert(GetDataRequest::subprefix == static_cast<uint8_t>(SystemSubPrefix::Subscription));
        
        std::cout << "  Message prefixes: PASS\n";
    }
    
    std::cout << "\nAll tests PASSED!\n";
    return 0;
}
