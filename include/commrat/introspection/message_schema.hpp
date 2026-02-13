/**
 * @file message_schema.hpp
 * @brief Complete schema combining CommRaT metadata with SeRTial layout
 * 
 * Provides MessageSchema<T, Registry> that includes:
 * - CommRaT compile-time metadata (message IDs, type names, size bounds)
 * - SeRTial structural layout (fields, types, offsets, sizes)
 * 
 * This structure is rfl-reflectable and can be exported to JSON/YAML/TOML/etc.
 * for use in logger, viewer, and debugging tools.
 * 
 * @author CommRaT Development Team
 * @date February 12, 2026
 */

#pragma once

#include "commrat/messages.hpp"
#include <sertial/core/layout/struct_layout.hpp>
#include <rfl.hpp>
#include <string_view>
#include <cstdint>

namespace commrat {

/**
 * @brief Complete schema for a CommRaT message type
 * 
 * Combines compile-time CommRaT metadata (message IDs, registry info)
 * with SeRTial's structural layout (fields, types, sizes, offsets).
 * This structure is rfl-reflectable and can be exported to JSON/YAML/etc.
 * 
 * @tparam PayloadT The user payload type
 * @tparam Registry The CommRaT registry containing this message
 * 
 * **Usage:**
 * @code
 * using Schema = MessageSchema<TemperatureData, MyApp>;
 * 
 * // Access CommRaT metadata
 * constexpr uint32_t id = Schema{}.commrat.message_id;
 * 
 * // Access SeRTial layout
 * constexpr size_t size = Schema{}.layout.base_packed_size;
 * 
 * // Export to JSON
 * auto json = rfl::json::write(Schema{});
 * @endcode
 */
template<typename PayloadT, typename Registry>
struct MessageSchema {
    /**
     * @brief CommRaT-specific metadata (compile-time constants)
     * 
     * Contains messaging-layer information computed at compile time:
     * - Message ID for routing and filtering
     * - Type names for debugging and display
     * - Size bounds for buffer allocation
     * - Registry identification for multi-application systems
     */
    struct CommRaTMetadata {
        uint32_t message_id = Registry::template get_message_id<PayloadT>();
        std::string payload_type = rfl::type_name_t<PayloadT>().str();
        std::string full_type = rfl::type_name_t<TimsMessage<PayloadT>>().str();
        size_t max_message_size = Registry::max_message_size;
        std::string registry_name = rfl::type_name_t<Registry>().str();
    };
    
    CommRaTMetadata commrat;
    sertial::StructLayout<TimsMessage<PayloadT>> layout;
};

} // namespace commrat
