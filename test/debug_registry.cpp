/**
 * @file debug_registry.cpp
 * @brief Debug what's actually in the expanded registry
 */

#include <commrat/commrat.hpp>
#include <iostream>

using namespace commrat;

// Simple test messages
struct TempData { float value; };
struct PressData { float value; };

struct CalCmd { uint8_t id; };
struct CalReply { bool ok; };

using MyApp = CommRaT<
    Message::Data<TempData>,
    Message::Data<PressData>,
    Message::Command<CalCmd, CalReply>
>;

template<typename Tuple, std::size_t I = 0>
void print_message_defs() {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        using MsgDef = std::tuple_element_t<I, Tuple>;
        
        std::cout << I << ": ";
        std::cout << __PRETTY_FUNCTION__ << "\n   ";
        std::cout << "prefix=" << static_cast<int>(MsgDef::prefix) << " ";
        std::cout << "subprefix=" << static_cast<int>(MsgDef::subprefix) << " ";
        std::cout << "local_id=" << MsgDef::local_id << " ";
        std::cout << "has_reply=" << MsgDef::has_reply << " ";
        std::cout << "is_request=" << MsgDef::is_request << "\n";
        
        print_message_defs<Tuple, I + 1>();
    }
}

int main() {
    std::cout << "MyApp Messages (" << MyApp::size() << " total):\n";
    std::cout << "===============================================\n";
    
    using MessageDefs = MyApp::MessageDefsTuple;
    print_message_defs<MessageDefs>();
    
    std::cout << "\nConstants:\n";
    std::cout << "MessagePrefix::System = " << static_cast<int>(MessagePrefix::System) << "\n";
    std::cout << "MessagePrefix::UserDefined = " << static_cast<int>(MessagePrefix::UserDefined) << "\n";
    std::cout << "UserSubPrefix::Data = " << static_cast<int>(UserSubPrefix::Data) << "\n";
    std::cout << "UserSubPrefix::Commands = " << static_cast<int>(UserSubPrefix::Commands) << "\n";
    std::cout << "SystemSubPrefix::Subscription = " << static_cast<int>(SystemSubPrefix::Subscription) << "\n";
    
    return 0;
}
