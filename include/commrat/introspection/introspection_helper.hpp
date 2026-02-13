/**
 * @file introspection_helper.hpp
 * @brief Registry-wide introspection and schema export
 * 
 * Provides IntrospectionHelper<Registry> for exporting message schemas
 * to any format supported by rfl (JSON, YAML, TOML, XML, etc.).
 * 
 * Exports MessageSchema<T, Registry> which combines:
 * - CommRaT metadata (message IDs, type names, size bounds)
 * - SeRTial layout (fields, types, offsets, sizes)
 * 
 * @author CommRaT Development Team
 * @date February 12, 2026
 */

#pragma once

#include "commrat/introspection/message_schema.hpp"
#include <sertial/core/layout/struct_layout_reflector.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <tuple>

namespace commrat {

/**
 * @brief Registry-wide introspection and schema export
 * 
 * Provides methods to export complete message schemas (CommRaT metadata + SeRTial layout)
 * for individual messages or all messages in a registry.
 * 
 * Format-agnostic - supports any rfl format (JSON, YAML, TOML, XML, etc.) via Writer type.
 * 
 * @tparam UserRegistry The CommRaT registry to introspect
 * 
 * **Usage:**
 * @code
 * using MyApp = CommRaT<Message::Data<TempData>, Message::Data<StatusData>>;
 * 
 * // Export single message schema to JSON (default)
 * auto json = MyApp::Introspection::export_as<TempData>();
 * 
 * // Export single message schema to YAML
 * auto yaml = MyApp::Introspection::export_as<TempData, rfl::yaml::Writer>();
 * 
 * // Export all message schemas to JSON
 * auto all_json = MyApp::Introspection::export_all();
 * 
 * // Export all message schemas to TOML
 * auto all_toml = MyApp::Introspection::export_all<rfl::toml::Writer>();
 * 
 * // Write all schemas to file
 * MyApp::Introspection::write_to_file<rfl::json>("schemas.json");
 * @endcode
 */
template<typename UserRegistry>
struct IntrospectionHelper {
    /**
     * @brief Export complete schema for a message type
     * 
     * Exports MessageSchema<T, Registry> which includes:
     * - CommRaT metadata (message_id, type_names, max_size)
     * - TimsMessage<T> structure (header + payload)
     * - SeRTial layout (fields, offsets, sizes, variable flags)
     * 
     * @tparam T Payload type (must be registered in UserRegistry)
     * @tparam Format rfl format (json, yaml, toml, xml, etc.)
     * @return Formatted string containing complete message schema
     * 
     * **Example Output (JSON):**
     * @code{.json}
     * {
     *   "commrat": {
     *     "message_id": 2847561283,
     *     "payload_type": "TemperatureData",
     *     "full_type": "commrat::TimsMessage<TemperatureData>",
     *     "max_message_size": 4096,
     *     "registry_name": "MyApp"
     *   },
     *   "layout": {
     *     "num_fields": 2,
     *     "sizeof_bytes": 72,
     *     "base_packed_size": 68,
     *     "max_packed_size": 68,
     *     "has_variable_fields": false,
     *     "fields": [
     *       {
     *         "name": "header",
     *         "type": "TimsHeader",
     *         "offset": 0,
     *         "size": 64,
     *         "is_variable": false
     *       },
     *       {
     *         "name": "payload",
     *         "type": "TemperatureData", 
     *         "offset": 64,
     *         "size": 8,
     *         "is_variable": false
     *       }
     *     ]
     *   }
     * }
     * @endcode
     * 
     * @tparam T Payload type to export schema for
     * @tparam Writer rfl Writer type (default: rfl::json::Writer)
     * @return Formatted string containing the message schema
     * 
     * @note All metadata computed at compile time - no runtime cost
     * @see MessageSchema for complete schema structure
     */
    template<typename T, typename Writer = rfl::json::Writer>
    static std::string export_as() {
        using Schema = MessageSchema<T, UserRegistry>;
        return rfl::json::write(Schema{});
    }
    
    /**
     * @brief Export complete schemas for all registered messages
     * 
     * Returns array/collection of MessageSchema instances, one per registered type.
     * Each contains full CommRaT metadata + SeRTial layout.
     * 
     * Useful for:
     * - Generating documentation for all message types
     * - Providing schema catalog to viewer/logger tools
     * - Runtime type discovery and validation
     * 
     * @tparam Format rfl format (json, yaml, toml, xml, etc.)
     * @return Formatted collection (array/list) of all message schemas
     * 
     * **Example Output (JSON):**
     * @code{.json}
     * [
     *   {
     *     "commrat": { "message_id": 123, "payload_type": "TempData", ... },
     *     "layout": { "num_fields": 2, ... }
     *   },
     *   {
     *     "commrat": { "message_id": 456, "payload_type": "StatusData", ... },
     *     "layout": { "num_fields": 3, ... }
     *   }
     * ]
     * @endcode
     * 
     * @tparam Writer rfl Writer type (default: rfl::json::Writer)
     * @return Formatted collection of all message schemas
     * 
     * @see export_as for single message export
     * @see write_to_file for file output convenience
     */
    template<typename Writer = rfl::json::Writer>
    static std::string export_all() {
        using PayloadTuple = typename UserRegistry::PayloadTypes;
        return export_all_impl<Writer>(PayloadTuple{});
    }
    
    /**
     * @brief Convenience: write all schemas to file
     * 
     * Exports all message schemas and writes to specified file.
     * File format determined by Format template parameter.
     * 
     * @tparam Format rfl format (json, yaml, toml, xml, etc.)
     * @param filename Output file path
     * 
     * **Usage:**
     * @code
     * // Write JSON schemas (default)
     * MyApp::Introspection::write_to_file("schemas.json");
     * 
     * // Write YAML schemas
     * MyApp::Introspection::write_to_file<rfl::yaml::Writer>("schemas.yaml");
     * 
     * // Write TOML schemas
     * MyApp::Introspection::write_to_file<rfl::toml::Writer>("schemas.toml");
     * @endcode
     * 
     * @tparam Writer rfl Writer type (default: rfl::json::Writer)
     * @param filename Output file path
     * 
     * @note Creates file if it doesn't exist, overwrites if it does
     */
    template<typename Writer = rfl::json::Writer>
    static void write_to_file(const std::string& filename) {
        auto data = export_all<Writer>();
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        file << data;
    }
    
private:
    /**
     * @brief Helper to export tuple of payload types as MessageSchema instances
     * 
     * Creates a tuple of all MessageSchema instances and serializes it directly.
     * rfl handles the heterogeneous tuple serialization automatically.
     * 
     * @tparam Writer rfl Writer type  
     * @tparam PayloadTypes Payload types from registry (unpacked from tuple)
     */
    template<typename Writer, typename... PayloadTypes>
    static std::string export_all_impl(std::tuple<PayloadTypes...>) {
        // Create tuple of all schemas - rfl will serialize this directly!
        auto all_schemas = std::make_tuple(
            MessageSchema<PayloadTypes, UserRegistry>{}...
        );
        
        // Let rfl do its magic - it serializes tuples as arrays
        return rfl::json::write(all_schemas);
    }
};

} // namespace commrat
