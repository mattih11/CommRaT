/**
 * @file introspection_helper.hpp
 * @brief Registry-wide introspection and schema export
 *
 * Provides IntrospectionHelper<Registry> for exporting message schemas
 * to any format supported by rfl (JSON, YAML, TOML, XML, etc.).
 *
 * Produces CommRaTSchemaOutput — a sertial::SchemaOutputT<CommRaTMessageRecord>
 * that extends sertial's standard format with CommRaT metadata (message IDs,
 * registry name, type names) in a separate "commrat" block per message.
 *
 * sertial-inspect can read CommRaT output files: it reads the "layout" field
 * in each record and ignores the "commrat" block.
 * commrat inspect reads both blocks.
 *
 * @author CommRaT Development Team
 * @date February 12, 2026
 */

#pragma once

#include "commrat/meta/schema_output.hpp"
#include "commrat/meta/message_schema.hpp"
#include <corerat/messaging/wire_message.hpp>
#include <sertial/integration/schema_export.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <tuple>

namespace commrat {

/**
 * @brief Registry-wide introspection and schema export
 *
 * Exports CommRaTSchemaOutput containing one CommRaTMessageRecord per
 * registered message type.  Each record has:
 *   - "layout": sertial StructLayout JSON string (sertial-inspect compatible)
 *   - "commrat": CommRaT metadata (message_id, type names, registry name)
 *
 * @tparam UserRegistry The CommRaT registry to introspect
 *
 * **Usage:**
 * @code
 * using MyApp = CommRaT<Message::Data<TempData>, Message::Data<StatusData>>;
 *
 * // Export single message schema (full StructLayout object, for compile-time use)
 * auto json = MyApp::Introspection::export_as<TempData>();
 *
 * // Export all messages as CommRaTSchemaOutput JSON
 * auto json = MyApp::Introspection::export_all();
 *
 * // Write CommRaTSchemaOutput to file
 * MyApp::Introspection::write_to_file("schemas.json");
 * @endcode
 */
template<typename UserRegistry>
struct IntrospectionHelper {
    /**
     * @brief Export complete schema for a single message type.
     *
     * Returns MessageSchema<T, Registry> serialized to JSON — useful for
     * compile-time layout inspection (field offsets, packed sizes, etc.).
     * For the standard multi-message output format use export_all().
     *
     * @tparam T Payload type (must be registered in UserRegistry)
     * @return JSON string containing MessageSchema (commrat block + StructLayout object)
     */
    template<typename T>
    static std::string export_as() {
        using Schema = MessageSchema<T, UserRegistry>;
        return rfl::json::write(Schema{});
    }

    /**
     * @brief Export all registered messages as CommRaTSchemaOutput JSON.
     *
     * Produces the canonical CommRaT schema format: a sertial SchemaOutputT
     * envelope with one CommRaTMessageRecord per message type.  Each record
     * has a serialized "layout" string (sertial-inspect compatible) plus a
     * "commrat" metadata block.
     *
     * @return JSON string of CommRaTSchemaOutput
     */
    static std::string export_all() {
        using PayloadTuple = typename UserRegistry::PayloadTypes;
        auto output = build_output(PayloadTuple{});
        return rfl::json::write(output);
    }

    /**
     * @brief Write CommRaTSchemaOutput to file.
     *
     * @param filename Output file path
     */
    static void write_to_file(const std::string& filename) {
        auto data = export_all();
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        file << data;
    }

private:
    template<typename... PayloadTypes>
    static CommRaTSchemaOutput build_output(std::tuple<PayloadTypes...>) {
        CommRaTSchemaOutput output;
        output.version   = "5.1.0";
        output.generated = __DATE__ " " __TIME__;
        (append_record<PayloadTypes>(output), ...);
        return output;
    }

    template<typename T>
    static void append_record(CommRaTSchemaOutput& output) {
        CommRaTMessageRecord record;
        record.layout = sertial::export_layout_data<corerat::WireMessage<T>>();
        record.commrat = {
            UserRegistry::template get_message_id<T>(),
            std::string(rfl::type_name_t<T>().str()),
            std::string(rfl::type_name_t<corerat::WireMessage<T>>().str()),
            UserRegistry::max_message_size,
            std::string(rfl::type_name_t<UserRegistry>().str())
        };
        output.messages.push_back(std::move(record));
    }
};

} // namespace commrat

