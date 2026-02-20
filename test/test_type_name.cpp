#include "commrat/module/helpers/type_name.hpp"
#include "commrat/module/module_config.hpp"
#include <cassert>
#include <iostream>

int test_enum_names() {
        
    enum class Color { Red, Green, Blue };
    enum class Shape { Circle, Square, Triangle };

    // Test get_type_name for enums
    // compile time checks for max_size_v and value
    static_assert(commrat::EnumName<Color>::max_size_v == 6,
        "Color enum has enumerator with name length = 6 (Green)");
    static_assert(commrat::EnumName<Shape>::max_size_v == 9,
        "Shape enum has enumerator with name length = 9 (Triangle)");
    
    constexpr Color green = Color::Green;
    constexpr Color red = Color::Red;
    constexpr Shape triangle = Shape::Triangle;
    constexpr Shape square = Shape::Square;

    constexpr auto green_name = commrat::EnumName<Color>::value<Color::Green>;
    constexpr auto red_name = commrat::EnumName<Color>::value<Color::Red>;
    constexpr auto triangle_name = commrat::EnumName<Shape>::value<Shape::Triangle>;
    constexpr auto square_name = commrat::EnumName<Shape>::value<Shape::Square>;

    static_assert(green_name == "Green", "Expected 'Green'");
    static_assert(red_name == "Red", "Expected 'Red'");
    static_assert(triangle_name == "Triangle", "Expected 'Triangle'");
    static_assert(square_name == "Square", "Expected 'Square'");

    static_assert(commrat::EnumName<Color>::value_t::max_size_v == 6, "Expected fixed_string with max_size_v = 6 for Color enum");
    static_assert(commrat::EnumName<Shape>::value_t::max_size_v == 9, "Expected fixed_string with max_size_v = 9 for Shape enum");

    //runtime checks for get_enum_name
    auto green_name_rt = commrat::EnumName<Color>::get(green);
    auto red_name_rt = commrat::EnumName<Color>::get(red);
    auto triangle_name_rt = commrat::EnumName<Shape>::get(triangle);
    auto square_name_rt = commrat::EnumName<Shape>::get(square);
    assert(green_name_rt == "Green");
    assert(red_name_rt == "Red");
    assert(triangle_name_rt == "Triangle");
    assert(square_name_rt == "Square");
    
    return 0;
}

struct SensorData { float temperature; };
struct ActuatorCommand { int motor_speed; };
int test_type_names() {
    using SensorDataTypeName = commrat::TypeName<SensorData>;
    using ActuatorCommandTypeName = commrat::TypeName<ActuatorCommand>;
    // compile-time checks for type names
    static_assert(SensorDataTypeName::value == "SensorData", "Expected type name 'SensorData'");
    static_assert(ActuatorCommandTypeName::value == "ActuatorCommand", "Expected type name 'ActuatorCommand'");
    static_assert(SensorDataTypeName::value.size() == 10, "Expected type name length of 10 for 'SensorData'");
    static_assert(ActuatorCommandTypeName::value.size() == 15, "Expected type name length of 15 for 'ActuatorCommand'");

    // runtime checks for get()
    auto sensor_data_name = SensorDataTypeName::get();
    auto actuator_command_name = ActuatorCommandTypeName::get();

    std::cout << "SensorData type name: " << sensor_data_name << "\n";
    std::cout << "ActuatorCommand type name: " << actuator_command_name << "\n";

    assert(sensor_data_name == "SensorData");
    assert(actuator_command_name == "ActuatorCommand");
    assert(sensor_data_name.size() == 10); // "SensorData" has 10 characters
    assert(actuator_command_name.size() == 15); // "ActuatorCommand" has 15 characters

    // Test TypeNames for multiple types
    using MultipleTypes = commrat::TypeNames<SensorData, ActuatorCommand, int>;
    static_assert(MultipleTypes::max_size_v == 16, "Expected max_size_v to be 16 for longest type name 'ActuatorCommand' (15) + null terminator");
    auto sensor_data_name_multi = MultipleTypes::get<SensorData>();
    auto actuator_command_name_multi = MultipleTypes::get<ActuatorCommand>();
    auto int_name_multi = MultipleTypes::get<int>();
    assert(sensor_data_name_multi == "SensorData");
    assert(actuator_command_name_multi == "ActuatorCommand");
    assert(int_name_multi == "int");
    assert(sensor_data_name_multi.size() == 10);
    assert(actuator_command_name_multi.size() == 15);
    assert(int_name_multi.size() == 3); // "int" has 3 characters
    
    std::cout << "Multiple types test passed!\n";

    return 0;
}

struct MyModule {};

int test_compile_time_mailbox_names() {
    // Test compile-time mailbox name formatting (old full API)
    constexpr auto cmd_name = commrat::format_mailbox_name_ct<
        SensorData, MyModule, commrat::MailboxType, commrat::MailboxType::CMD, 10, 1>();
    
    static_assert(cmd_name == "MyModule_SensorData:10:1:CMD", "Expected compile-time mailbox name");
    
    std::cout << "Compile-time mailbox name (old API): " << cmd_name << "\n";
    assert(cmd_name == "MyModule_SensorData:10:1:CMD");
    
    // Test different IDs
    constexpr auto work_name = commrat::format_mailbox_name_ct<
        ActuatorCommand, MyModule, commrat::MailboxType, commrat::MailboxType::WORK, 255, 128>();
    
    std::cout << "Compile-time WORK mailbox (old API): " << work_name << "\n";
    assert(work_name == "MyModule_ActuatorCommand:255:128:WORK");
    
    // Test NEW MailboxNameBuilder (simplified without ModuleType)
    std::cout << "\nTesting MailboxNameBuilder (new simplified API)...\n";
    
    // Create compile-time builders
    constexpr auto cmd_builder = commrat::MailboxNameBuilder<
        SensorData, commrat::MailboxType::CMD>();
    
    constexpr auto publish_builder = commrat::MailboxNameBuilder<
        ActuatorCommand, commrat::MailboxType::PUBLISH>();
    
    // Verify compile-time constants
    static_assert(cmd_builder.data_name == "SensorData", "Data name extracted");
    static_assert(cmd_builder.mbx_type == "CMD", "Mailbox type extracted");
    
    // Format with runtime IDs
    auto name1 = cmd_builder.format(10, 1);
    auto name2 = publish_builder.format(42, 99);
    
    std::cout << "Builder CMD name: " << name1 << "\n";
    std::cout << "Builder PUBLISH name: " << name2 << "\n";
    
    assert(name1 == "SensorData:10:1:CMD");
    assert(name2 == "ActuatorCommand:42:99:PUBLISH");
    
    // Test helper function with runtime enum dispatch
    auto name3 = commrat::format_mailbox_name_from_type<SensorData>(
        10, 1, commrat::MailboxType::CMD);
    auto name4 = commrat::format_mailbox_name_from_type<ActuatorCommand>(
        255, 0, commrat::MailboxType::DATA);
    
    std::cout << "Helper CMD name: " << name3 << "\n";
    std::cout << "Helper DATA name: " << name4 << "\n";
    
    assert(name3 == "SensorData:10:1:CMD");
    assert(name4 == "ActuatorCommand:255:0:DATA");
    
    // Test DataMailboxNameBuilder with compile-time index
    std::cout << "\nTesting DataMailboxNameBuilder with compile-time index...\n";
    
    constexpr auto data0_builder = commrat::DataMailboxNameBuilder<SensorData, 0>();
    constexpr auto data1_builder = commrat::DataMailboxNameBuilder<ActuatorCommand, 1>();
    constexpr auto data15_builder = commrat::DataMailboxNameBuilder<SensorData, 15>();
    
    // Verify compile-time mailbox type generation
    static_assert(data0_builder.mbx_type == "DATA0", "DATA0 generated at compile time");
    static_assert(data1_builder.mbx_type == "DATA1", "DATA1 generated at compile time");
    static_assert(data15_builder.mbx_type == "DATA15", "DATA15 generated at compile time");
    
    auto data0_name = data0_builder.format(10, 1);
    auto data1_name = data1_builder.format(20, 2);
    auto data15_name = data15_builder.format(255, 128);
    
    std::cout << "DATA0 mailbox: " << data0_name << "\n";
    std::cout << "DATA1 mailbox: " << data1_name << "\n";
    std::cout << "DATA15 mailbox: " << data15_name << "\n";
    
    assert(data0_name == "SensorData:10:1:DATA0");
    assert(data1_name == "ActuatorCommand:20:2:DATA1");
    assert(data15_name == "SensorData:255:128:DATA15");
    
    std::cout << "DataMailboxNameBuilder test passed!\n";
    
    // Test uint8_to_fixed_string helper
    constexpr auto zero = commrat::uint8_to_fixed_string(0);
    constexpr auto one = commrat::uint8_to_fixed_string(1);
    constexpr auto ten = commrat::uint8_to_fixed_string(10);
    constexpr auto max = commrat::uint8_to_fixed_string(255);
    
    static_assert(zero == "0", "Expected '0'");
    static_assert(one == "1", "Expected '1'");
    static_assert(ten == "10", "Expected '10'");
    static_assert(max == "255", "Expected '255'");
    
    std::cout << "Compile-time uint8 conversion test passed!\n";
    
    return 0;
}

int main(int argc, char** argv) {
    // Test EnumName utility
    std::cout << "Running test_enum_names...\n";
    test_enum_names();
    
    std::cout << "Running test_type_names...\n";
    test_type_names();
    
    std::cout << "Running test_compile_time_mailbox_names...\n";
    test_compile_time_mailbox_names();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}