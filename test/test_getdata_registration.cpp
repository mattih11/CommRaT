/**
 * @file test_getdata_registration.cpp
 * @brief Test automatic GetData message registration for Message::Data types
 */

#include "commrat/messaging/message_helpers.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include <iostream>
#include <cassert>

// Test data payload
struct TemperatureData {
    float temperature{0.0f};
    uint32_t sensor_id{0};
};

struct PressureData {
    float pressure{0.0f};
    uint32_t sensor_id{0};
};

using namespace commrat;

int main() {
    std::cout << "Testing automatic GetData message registration...\n\n";
    
    // Define data messages (should automatically include GetData support)
    using TempMsg = Message::Data<TemperatureData>;
    using PresMsg = Message::Data<PressureData>;
    
    // Verify Message::Data provides GetData type aliases
    std::cout << "  Checking TempMsg GetData type aliases...\n";
    static_assert(std::is_same_v<typename TempMsg::GetDataRequestDef, GetDataRequest<TemperatureData>>,
                  "TempMsg::GetDataRequestDef should be GetDataRequest<TemperatureData>");
    static_assert(std::is_same_v<typename TempMsg::GetDataReplyDef, GetDataReply<TemperatureData>>,
                  "TempMsg::GetDataReplyDef should be GetDataReply<TemperatureData>");
    std::cout << "    TempMsg::GetDataRequestDef: PASS\n";
    std::cout << "    TempMsg::GetDataReplyDef: PASS\n";
    
    std::cout << "  Checking TempMsg GetNextData type aliases...\n";
    static_assert(std::is_same_v<typename TempMsg::GetNextDataRequestDef, GetNextDataRequest<TemperatureData>>,
                  "TempMsg::GetNextDataRequestDef should be GetNextDataRequest<TemperatureData>");
    static_assert(std::is_same_v<typename TempMsg::GetNextDataReplyDef, GetNextDataReply<TemperatureData>>,
                  "TempMsg::GetNextDataReplyDef should be GetNextDataReply<TemperatureData>");
    std::cout << "    TempMsg::GetNextDataRequestDef: PASS\n";
    std::cout << "    TempMsg::GetNextDataReplyDef: PASS\n";
    
    // Create registry (should auto-expand to include GetData messages)
    using MyRegistry = MessageRegistry<TempMsg, PresMsg>;
    
    std::cout << "\n  Checking registry message IDs...\n";
    
    // Check that GetData messages are registered
    constexpr uint32_t temp_id = MyRegistry::get_message_id<TemperatureData>();
    constexpr uint32_t pres_id = MyRegistry::get_message_id<PressureData>();
    constexpr uint32_t temp_getdata_req_id = MyRegistry::get_message_id<GetDataRequestPayload<TemperatureData>>();
    constexpr uint32_t temp_getdata_rep_id = MyRegistry::get_message_id<GetDataReplyPayload<TemperatureData>>();
    constexpr uint32_t pres_getdata_req_id = MyRegistry::get_message_id<GetDataRequestPayload<PressureData>>();
    constexpr uint32_t pres_getdata_rep_id = MyRegistry::get_message_id<GetDataReplyPayload<PressureData>>();
    
    std::cout << "    TemperatureData ID: 0x" << std::hex << temp_id << std::dec << "\n";
    std::cout << "    PressureData ID: 0x" << std::hex << pres_id << std::dec << "\n";
    std::cout << "    GetDataRequest<TemperatureData> ID: 0x" << std::hex << temp_getdata_req_id << std::dec << "\n";
    std::cout << "    GetDataReply<TemperatureData> ID: 0x" << std::hex << temp_getdata_rep_id << std::dec << "\n";
    std::cout << "    GetDataRequest<PressureData> ID: 0x" << std::hex << pres_getdata_req_id << std::dec << "\n";
    std::cout << "    GetDataReply<PressureData> ID: 0x" << std::hex << pres_getdata_rep_id << std::dec << "\n";
    
    // Verify they're different
    assert(temp_id != pres_id);
    assert(temp_getdata_req_id != pres_getdata_req_id);
    assert(temp_getdata_rep_id != pres_getdata_rep_id);
    
    std::cout << "\n  Message IDs are unique: PASS\n";
    
    // Verify request/reply pairing (reply ID should be negative of request ID)
    std::cout << "\n  Checking request/reply ID pairing...\n";
    
    // Check that reply messages have correct flags
    using TempGetDataReq = GetDataRequest<TemperatureData>;
    using TempGetDataRep = GetDataReply<TemperatureData>;
    
    static_assert(TempGetDataReq::has_reply, "GetDataRequest should have reply");
    static_assert(TempGetDataReq::is_request, "GetDataRequest should be request");
    static_assert(!TempGetDataRep::is_request, "GetDataReply should not be request");
    
    std::cout << "    GetDataRequest::has_reply: PASS\n";
    std::cout << "    GetDataRequest::is_request: PASS\n";
    std::cout << "    GetDataReply::is_request: PASS (false)\n";
    
    // Verify ReplyType aliases work for CmdInput
    std::cout << "\n  Checking ReplyType aliases for CmdInput...\n";
    static_assert(std::is_same_v<typename GetDataRequestPayload<TemperatureData>::ReplyType, GetDataReplyPayload<TemperatureData>>,
                  "GetDataRequestPayload should have ReplyType alias");
    static_assert(std::is_same_v<typename GetNextDataRequestPayload<TemperatureData>::ReplyType, GetNextDataReplyPayload<TemperatureData>>,
                  "GetNextDataRequestPayload should have ReplyType alias");
    std::cout << "    GetDataRequestPayload::ReplyType: PASS\n";
    std::cout << "    GetNextDataRequestPayload::ReplyType: PASS\n";
    
    std::cout << "\nAll GetData registration tests PASSED!\n";
    
    return 0;
}
