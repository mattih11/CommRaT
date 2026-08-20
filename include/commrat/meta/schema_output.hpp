/**
 * @file commrat_schema_output.hpp
 * @brief CommRaT typed schema output format, extending sertial::SchemaOutputT
 *
 * Defines the output type written by IntrospectionHelper::write_to_file()
 * and read by `commrat inspect`.
 *
 * **Format compatibility:**
 *   sertial-inspect reads the "layout" field in each record (ignores "commrat").
 *   commrat inspect reads both "layout" and "commrat".
 *   The types compose via sertial::SchemaOutputT<CommRaTMessageRecord> rather
 *   than via rfl::Flatten because sertial::SchemaGenerator sets record.layout
 *   by direct field access.
 */

#pragma once

#include <sertial/integration/schema_generator.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace commrat {

/**
 * @brief Runtime CommRaT metadata for one message type.
 *
 * Tool-facing struct — all fields are plain runtime values so the record
 * can be read/written by any rfl format without template instantiation.
 */
struct CommRaTMessageMetadata {
    uint32_t    message_id{};
    std::string payload_type;
    std::string full_type;
    std::size_t max_message_size{};
    std::string registry_name;
};

/**
 * @brief Per-message record in CommRaTSchemaOutput.
 *
 * Layout-compatible with sertial::MessageLayoutRecord: both have
 * `layout: std::string` as their first field, so sertial tooling that reads
 * only "layout" continues to work without modification.
 *
 * JSON structure:
 * @code{.json}
 * {
 *   "layout": "{ ...sertial StructLayout JSON... }",
 *   "commrat": {
 *     "message_id": 1,
 *     "payload_type": "TemperatureData",
 *     "full_type": "TimsMessage<TemperatureData>",
 *     "max_message_size": 4096,
 *     "registry_name": "MyApp"
 *   }
 * }
 * @endcode
 */
struct CommRaTMessageRecord {
    std::string            layout;   ///< sertial::export_layout_data<TimsMessage<T>>()
    CommRaTMessageMetadata commrat;
};

/**
 * @brief Top-level schema file format written by IntrospectionHelper.
 *
 * Uses sertial::SchemaOutputT so `sertial-inspect` can parse the outer
 * envelope (version, generated, messages[]) while ignoring the commrat block.
 */
using CommRaTSchemaOutput = sertial::SchemaOutputT<CommRaTMessageRecord>;

} // namespace commrat
