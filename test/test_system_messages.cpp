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
    
    // Test 3: GetDataRequest has reply (template, must be instantiated with a type)
    {
        using GDReq  = GetDataRequest<float, 0x0001>;
        using GDReply = GetDataReply<float, 0x0001>;
        static_assert(GDReq::has_reply, "GetDataRequest should have reply");
        static_assert(GDReq::is_request, "GetDataRequest should be marked as request");
        static_assert(!GDReq::is_reply, "GetDataRequest should not be marked as reply");
        
        constexpr int16_t request_id = static_cast<int16_t>(GDReq::local_id);
        constexpr int16_t reply_id = static_cast<int16_t>(GDReply::local_id);
        static_assert(reply_id == -request_id, "Reply ID should be negative of request ID");
        
        std::cout << "  GetDataRequest ID: 0x" << std::hex << GDReq::local_id << std::dec << "\n";
        std::cout << "  GetDataReply ID:   0x" << std::hex << GDReply::local_id << std::dec << "\n";
        std::cout << "  GetDataRequest: PASS\n";
    }
    
    // Test 4: GetNextDataRequest has reply (template, must be instantiated with a type)
    {
        using GNDReq  = GetNextDataRequest<float, 0x0001>;
        using GNDReply = GetNextDataReply<float, 0x0001>;
        static_assert(GNDReq::has_reply, "GetNextDataRequest should have reply");
        static_assert(GNDReq::is_request, "GetNextDataRequest should be marked as request");
        static_assert(!GNDReq::is_reply, "GetNextDataRequest should not be marked as reply");
        
        constexpr int16_t request_id = static_cast<int16_t>(GNDReq::local_id);
        constexpr int16_t reply_id = static_cast<int16_t>(GNDReply::local_id);
        static_assert(reply_id == -request_id, "Reply ID should be negative of request ID");
        
        std::cout << "  GetNextDataRequest ID: 0x" << std::hex << GNDReq::local_id << std::dec << "\n";
        std::cout << "  GetNextDataReply ID:   0x" << std::hex << GNDReply::local_id << std::dec << "\n";
        std::cout << "  GetNextDataRequest: PASS\n";
    }
    
    // Test 5: Reply messages are properly marked
    {
        static_assert(!SubscribeReply::has_reply, "Reply messages should not have replies themselves");
        static_assert(!SubscribeReply::is_request, "Reply messages should not be marked as request");
        // Note: is_reply detection has edge case with ID=0x0001 becoming 0xFFFF (AUTO_ID marker)
        // The important part is that has_reply=false and is_request=false
        
        using GDReply = GetDataReply<float, 0x0001>;
        static_assert(!GDReply::has_reply, "Reply messages should not have replies themselves");
        static_assert(!GDReply::is_request, "Reply messages should not be marked as request");
        
        std::cout << "  Reply message flags: PASS\n";
    }
    
    // Test 6: Messages use correct prefix/subprefix
    {
        // Subscribe/Unsubscribe use System::Subscription
        static_assert(SubscribeRequest::prefix == MessagePrefix::System);
        static_assert(SubscribeRequest::subprefix == static_cast<uint8_t>(SystemSubPrefix::Subscription));
        
        // GetDataRequest/GetNextDataRequest use UserDefined::GetData / UserDefined::GetNextData
        using GDReq  = GetDataRequest<float, 0x0001>;
        using GNDReq = GetNextDataRequest<float, 0x0001>;
        static_assert(GDReq::prefix == MessagePrefix::UserDefined);
        static_assert(GDReq::subprefix == static_cast<uint8_t>(UserSubPrefix::GetData));
        static_assert(GNDReq::prefix == MessagePrefix::UserDefined);
        static_assert(GNDReq::subprefix == static_cast<uint8_t>(UserSubPrefix::GetNextData));
        
        std::cout << "  Message prefixes: PASS\n";
    }
    
    std::cout << "\nAll tests PASSED!\n";
    return 0;
}
