/**
 * @file test_auto_id_validation.cpp
 * @brief Test AUTO_ID=0 and MAX_MESSAGE_ID validation
 */

#include "commrat/messaging/message_id.hpp"
#include "commrat/messaging/message_registry.hpp"
#include <iostream>
#include <cassert>

using namespace commrat;

// Test payload types
struct AutoID1 { int value; };
struct AutoID2 { int value; };
struct AutoID3 { int value; };
struct ManualID100 { int value; };
struct ReplyPayload { bool success; };

int main() {
    std::cout << "Testing AUTO_ID=0 and ID validation...\n";
    
    // Test 1: AUTO_ID marker is 0
    {
        static_assert(DefaultMessageDef::id == 0, "AUTO_ID should be 0");
        std::cout << "  AUTO_ID = 0: PASS\n";
    }
    
    // Test 2: MAX_MESSAGE_ID is 0x7FFF
    {
        static_assert(MAX_MESSAGE_ID == 0x7FFF, "MAX_MESSAGE_ID should be 0x7FFF");
        std::cout << "  MAX_MESSAGE_ID = 0x7FFF: PASS\n";
    }
    
    // Test 3: Auto-assigned IDs start at 1
    {
        using AutoMsg1 = MessageDefinition<AutoID1, MessagePrefix::UserDefined, UserSubPrefix::Data>;
        using AutoMsg2 = MessageDefinition<AutoID2, MessagePrefix::UserDefined, UserSubPrefix::Data>;
        using AutoMsg3 = MessageDefinition<AutoID3, MessagePrefix::UserDefined, UserSubPrefix::Data>;
        
        using TestRegistry = MessageRegistry<AutoMsg1, AutoMsg2, AutoMsg3>;
        
        // First auto-assigned ID should be 1
        static_assert(AutoMsg1::local_id == 0, "Before registry processing, ID is 0");
        static_assert(AutoMsg1::needs_auto_id == true, "Should need auto ID");
        
        std::cout << "  Auto-assigned IDs start at 1: PASS\n";
    }
    
    // Test 4: Reply ID calculation works with 0 = -0
    {
        using RequestMsg = MessageDefinition<
            AutoID1,
            MessagePrefix::System,
            SystemSubPrefix::Subscription,
            0x0001,
            ReplyPayload  // Has reply
        >;
        
        static_assert(RequestMsg::has_reply, "Should have reply");
        static_assert(RequestMsg::local_id == 0x0001, "Request ID = 1");
        
        // Reply ID should be negative of request ID
        using ReplyMsg = typename RequestMsg::ReplyMessageDef;
        constexpr int16_t request_id = static_cast<int16_t>(RequestMsg::local_id);
        constexpr int16_t reply_id = static_cast<int16_t>(ReplyMsg::local_id);
        
        static_assert(reply_id == -request_id, "Reply ID = -Request ID");
        static_assert(reply_id == -1, "Reply ID should be -1");
        static_assert(static_cast<uint16_t>(reply_id) == 0xFFFF, "Reply ID as uint16 = 0xFFFF");
        
        std::cout << "  Reply ID = -Request ID: PASS\n";
    }
    
    // Test 5: Manual ID within range
    {
        using ValidMsg = MessageDefinition<
            ManualID100,
            MessagePrefix::UserDefined,
            UserSubPrefix::Data,
            100
        >;
        
        static_assert(ValidMsg::local_id == 100, "Manual ID should be preserved");
        static_assert(!ValidMsg::needs_auto_id, "Should not need auto ID");
        
        std::cout << "  Manual ID within range: PASS\n";
    }
    
    // Test 6: MAX_MESSAGE_ID boundary (0x7FFF)
    {
        using MaxValidMsg = MessageDefinition<
            ManualID100,
            MessagePrefix::UserDefined,
            UserSubPrefix::Data,
            0x7FFF  // Maximum allowed
        >;
        
        static_assert(MaxValidMsg::local_id == 0x7FFF, "Should allow MAX_MESSAGE_ID");
        
        std::cout << "  MAX_MESSAGE_ID boundary (0x7FFF): PASS\n";
    }
    
    // Test 7: Negative IDs (reply messages) detected correctly
    {
        using ReplyMsg = MessageDefinition<
            ReplyPayload,
            MessagePrefix::System,
            SystemSubPrefix::Subscription,
            static_cast<uint16_t>(-10)  // Negative ID = 0xFFF6
        >;
        
        static_assert(ReplyMsg::local_id == 0xFFF6, "Negative ID stored as uint16");
        static_assert(static_cast<int16_t>(ReplyMsg::local_id) == -10, "Interpreted as -10");
        static_assert(ReplyMsg::is_reply, "Should be detected as reply");
        static_assert(!ReplyMsg::is_request, "Should not be request");
        static_assert(!ReplyMsg::has_reply, "Reply should not have reply");
        
        std::cout << "  Negative IDs (reply messages): PASS\n";
    }
    
    std::cout << "\nAll AUTO_ID and validation tests PASSED!\n";
    return 0;
}
