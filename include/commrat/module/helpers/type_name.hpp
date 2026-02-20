/**
 * @file type_name.hpp
 * @brief Compile-time type name extraction using reflect-cpp
 * 
 * Provides human-readable type names at compile time for debugging and logging.
 * Used to generate readable mailbox names like "SensorA:10:1:CMD" instead of
 * mangled names like "7SensorA_cmd_18088192".
 * 
 * @author CommRaT Development Team
 * @date February 15, 2026
 */

#pragma once

#include <rfl.hpp>
#include <sertial/containers/fixed_string.hpp>
#include "commrat/mailbox/mailbox_type.hpp"

#include <string_view>
#include <array>
#include <cstddef>

namespace commrat {

/**
* @brief Get max length of an enum-member string at compile time
* TODO move to SeRTial as StringUtility
*/
template<typename EnumType> requires std::is_enum_v<EnumType>
struct EnumName {
    static constexpr std::size_t max_size_v = []() -> std::size_t {  
        constexpr auto enumerator_array = rfl::get_enumerator_array<EnumType>();
        std::size_t max_length = 0;

        for(const auto& enumerator : enumerator_array) {
            // +1 for null terminator
            max_length = std::max(max_length, 1 + enumerator.first.size());
        }

        return max_length;
    }();
    
    using value_t = sertial::fixed_string<max_size_v>;

    template<EnumType Value>
    static constexpr value_t value = []() -> value_t {
        constexpr auto enumerator_array = rfl::get_enumerator_array<EnumType>();
        
        // Find matching enumerator
        for (const auto& [name, val] : enumerator_array) {
            if (val == Value) {
                return name;
            }
        }
        return "";  // Not found (shouldn't happen for valid enum values)
    }();

    static const value_t get(const EnumType value) {
        return value_t(rfl::enum_to_string(value));
    }
};


/**
 * @brief Compile-time type name extraction
 * 
 * Provides clean, unmangled type names at compile time using reflect-cpp.
 * Returns a fixed_string that can be used in constexpr contexts.
 * 
 * Example:
 * @code
 * struct SensorData {};
 * constexpr auto name = TypeName<SensorData>::value;  // "SensorData"
 * auto name_view = TypeName<SensorData>::get();       // Returns string_view
 * @endcode
 * 
 * TODO move to SeRTial as StringUtility
 */
template<typename T>
struct TypeName {
    static constexpr auto value = sertial::make_fixed(rfl::internal::get_type_name<T>());
    
    static constexpr auto get() {
        return value;  // Return the fixed_string directly
    }
};

/**
 * @brief Multiple type names with unified max size
 * 
 * Computes the maximum length among multiple type names and provides
 * fixed_string instances sized to hold any of them.
 * 
 * Example:
 * @code
 * using Names = TypeNames<SensorData, ActuatorCommand, int>;
 * static_assert(Names::max_size_v == 15);  // "ActuatorCommand" is longest
 * auto name = Names::get<SensorData>();    // Returns fixed_string<15>
 * @endcode
 * 
 * TODO move to SeRTial as StringUtility
 */
template<typename... Types>
struct TypeNames {
    static constexpr std::size_t max_size_v = []() {
        std::size_t max_len = 0;
        ((max_len = std::max(max_len, TypeName<Types>::value.size())), ...);
        return max_len + 1;  // +1 for null terminator
    }();
    
    using value_t = sertial::fixed_string<max_size_v>;
    
    template<typename T>
    static constexpr value_t get() {
        return value_t(std::string_view(TypeName<T>::value));
    }
};

/**
 * @brief Helper function for getting type name as string_view
 * 
 * Provides a convenient function interface for type name extraction.
 * 
 * Example:
 * @code
 * auto name = get_type_name<SensorData>();  // Returns string_view "SensorData"
 * @endcode
 * 
 * TODO move to SeRTial as StringUtility
 */
template<typename T>
constexpr std::string_view get_type_name() {
    return std::string_view(TypeName<T>::value);
}

/**
 * @brief Constexpr helper to convert uint8_t to decimal string at compile time
 * 
 * TODO move to SeRTial as StringUtility
 */
constexpr auto uint8_to_fixed_string(uint8_t value) {
    sertial::fixed_string<4> result;  // Max "255" + null = 4
    
    if (value == 0) {
        result.push_back('0');
        return result;
    }
    
    // Build string in reverse
    sertial::fixed_string<4> temp;
    uint8_t v = value;
    while (v > 0) {
        temp.push_back('0' + (v % 10));
        v /= 10;
    }
    
    // Reverse into result
    for (std::size_t i = temp.size(); i > 0; --i) {
        result.push_back(temp[i - 1]);
    }
    
    return result;
}

/**
 * @brief Format mailbox name at compile time: "TypeName:sys:inst:MailboxType"
 * 
 * Fully constexpr version using template parameters for compile-time generation.
 * 
 * @tparam DataType The message type
 * @tparam ModuleType The module type (for module name)
 * @tparam MbxTypeEnum Enum type for mailbox type (e.g., MailboxType)
 * @tparam MbxTypeValue The mailbox type enum value
 * @tparam SystemId System ID (0-255)
 * @tparam InstanceId Instance ID (0-255)
 * @return constexpr fixed_string with formatted name
 * 
 * Example:
 * @code
 * constexpr auto name = format_mailbox_name_ct<SensorData, MyModule, MailboxType, MailboxType::CMD, 10, 1>();
 * // Result: "MyModule_SensorData:10:1:CMD"
 * @endcode
 */
template<typename DataType, typename ModuleType, typename MbxTypeEnum, MbxTypeEnum MbxTypeValue, 
         uint8_t SystemId, uint8_t InstanceId>
    requires std::is_enum_v<MbxTypeEnum>
constexpr auto format_mailbox_name_ct() {
    constexpr auto module_name = TypeName<ModuleType>::value;
    constexpr auto data_name = TypeName<DataType>::value;
    constexpr auto mbx_type = EnumName<MbxTypeEnum>::template value<MbxTypeValue>;
    constexpr auto sys_id_str = uint8_to_fixed_string(SystemId);
    constexpr auto inst_id_str = uint8_to_fixed_string(InstanceId);
    
    // Use sertial's fixed_string concatenation with compile-time size deduction
    return module_name + sertial::make_fixed<"_">() + data_name + sertial::make_fixed<":">() 
         + sys_id_str + sertial::make_fixed<":">() 
         + inst_id_str + sertial::make_fixed<":">() 
         + mbx_type;
}

/**
 * @brief Runtime version for backward compatibility
 * 
 * @tparam T The message type
 * @param system_id System ID (0-255)
 * @param instance_id Instance ID (0-255)
 * @param mailbox_type Mailbox type suffix
 * @return fixed_string<128> with formatted name
 */
template<typename T>
inline auto format_mailbox_name(uint8_t system_id, uint8_t instance_id, 
                                std::string_view mailbox_type) {
    auto type_name = TypeName<T>::value;
    auto sys_str = uint8_to_fixed_string(system_id);
    auto inst_str = uint8_to_fixed_string(instance_id);
    
    // Use sertial's fixed_string concatenation
    return type_name + sertial::make_fixed<":">() 
         + sys_str + sertial::make_fixed<":">() 
         + inst_str + sertial::make_fixed<":">() 
         + sertial::fixed_string(mailbox_type);
}

/**
 * @brief Runtime version with module name prefix (backward compatibility)
 * 
 * @tparam T The message type
 * @param module_name Module name prefix
 * @param system_id System ID (0-255)
 * @param instance_id Instance ID (0-255)
 * @param mailbox_type Mailbox type suffix
 * @return fixed_string<256> with formatted name
 */
template<typename T>
inline auto format_mailbox_name_with_prefix(std::string_view module_name,
                                             uint8_t system_id, uint8_t instance_id, 
                                             std::string_view mailbox_type) {
    sertial::fixed_string<256> result;
    
    for (char c : module_name) result.push_back(c);
    result.push_back('_');
    
    auto type_name = get_type_name<T>();
    for (char c : type_name) result.push_back(c);
    result.push_back(':');
    
    auto sys_str = uint8_to_fixed_string(system_id);
    for (char c : sys_str) result.push_back(c);
    result.push_back(':');
    
    auto inst_str = uint8_to_fixed_string(instance_id);
    for (char c : inst_str) result.push_back(c);
    result.push_back(':');
    
    for (char c : mailbox_type) result.push_back(c);
    
    return result;
}

/**
 * @brief Format DATA mailbox name with index (runtime)
 * 
 * @tparam T The message type
 * @param system_id System ID (0-255)
 * @param instance_id Instance ID (0-255)
 * @param input_index Input index (0, 1, 2, ...)
 * @return fixed_string<128> with formatted name
 */
template<typename T>
inline auto format_data_mailbox_name(uint8_t system_id, uint8_t instance_id, 
                                      uint8_t input_index) {
    auto idx_str = uint8_to_fixed_string(input_index);
    auto mailbox_type = sertial::make_fixed<"DATA">() + idx_str;
    
    return format_mailbox_name<T>(system_id, instance_id, std::string_view(mailbox_type));
}

/**
 * @brief Compile-time mailbox name builder - only sys/inst IDs are runtime
 * 
 * Computes everything at compile time except system_id and instance_id.
 * Returns a pre-sized fixed_string with compile-time known prefix/suffix.
 * 
 * @tparam DataType The message type
 * @tparam MbxTypeValue The mailbox type enum value
 * 
 * Example:
 * @code
 * constexpr auto builder = MailboxNameBuilder<SensorData, MailboxType::CMD>();
 * auto name = builder.format(10, 1);  // "SensorData:10:1:CMD"
 * @endcode
 */
template<typename DataType, MailboxType MbxTypeValue>
struct MailboxNameBuilder {
    // Compile-time constants
    static constexpr auto data_name = TypeName<DataType>::value;
    static constexpr auto mbx_type = EnumName<MailboxType>::template value<MbxTypeValue>;
    
    /**
     * @brief Format mailbox name with runtime sys/inst IDs
     */
    static constexpr auto format(uint8_t system_id, uint8_t instance_id) {
        auto sys_str = uint8_to_fixed_string(system_id);
        auto inst_str = uint8_to_fixed_string(instance_id);
        
        // Use sertial's fixed_string concatenation
        return data_name + sertial::make_fixed<":">()
             + sys_str + sertial::make_fixed<":">()
             + inst_str + sertial::make_fixed<":">()
             + mbx_type;
    }
};

/**
 * @brief Compile-time DATA mailbox name builder with index
 * 
 * Specialized builder for DATA mailboxes that includes the input index.
 * Format: "DataType:sys:inst:DATA0" (or DATA1, DATA2, etc.)
 * 
 * @tparam DataType The message type
 * @tparam InputIndex The input index (0, 1, 2, ...)
 * 
 * Example:
 * @code
 * constexpr auto builder = DataMailboxNameBuilder<SensorData, 0>();
 * auto name = builder.format(10, 1);  // "SensorData:10:1:DATA0"
 * @endcode
 */
template<typename DataType, uint8_t InputIndex>
struct DataMailboxNameBuilder {
    // Compile-time constants
    static constexpr auto data_name = TypeName<DataType>::value;
    static constexpr auto idx_str = uint8_to_fixed_string(InputIndex);
    
    // Build "DATAN" at compile time
    static constexpr auto mbx_type = sertial::make_fixed<"DATA">() + idx_str;
    
    /**
     * @brief Format mailbox name with runtime sys/inst IDs
     */
    static constexpr auto format(uint8_t system_id, uint8_t instance_id) {
        auto sys_str = uint8_to_fixed_string(system_id);
        auto inst_str = uint8_to_fixed_string(instance_id);
        
        // Use sertial's fixed_string concatenation
        return data_name + sertial::make_fixed<":">() 
             + sys_str + sertial::make_fixed<":">() 
             + inst_str + sertial::make_fixed<":">() 
             + mbx_type;
    }
};

/**
 * @brief Helper to create mailbox name from address at runtime
 * Uses compile-time type info + runtime address decoding
 * 
 * @tparam DataType The message type
 * @param system_id System ID extracted from address or config
 * @param instance_id Instance ID extracted from address or config  
 * @param mbx_type The mailbox type enum value
 * @return fixed_string with formatted name
 */
template<typename DataType>
inline auto format_mailbox_name_from_type(uint8_t system_id, uint8_t instance_id, MailboxType mbx_type) {
    // Calculate max size needed at compile time
    constexpr auto data_name = TypeName<DataType>::value;
    constexpr std::size_t max_mbx_size = EnumName<MailboxType>::max_size_v;
    
    // Max size: data + ":" + "255:255" + ":" + max_enum_name
    constexpr std::size_t max_size = data_name.size() + 1 + 7 + 1 + max_mbx_size + 1;
    
    sertial::fixed_string<max_size> result;
    
    // DataType:
    for (char c : data_name) result.push_back(c);
    result.push_back(':');
    
    // SystemId:
    auto sys_str = uint8_to_fixed_string(system_id);
    for (char c : sys_str) result.push_back(c);
    result.push_back(':');
    
    // InstanceId:
    auto inst_str = uint8_to_fixed_string(instance_id);
    for (char c : inst_str) result.push_back(c);
    result.push_back(':');
    
    // MailboxType (runtime lookup)
    auto mbx_name = EnumName<MailboxType>::get(mbx_type);
    for (char c : mbx_name) result.push_back(c);
    
    return result;
}

} // namespace commrat