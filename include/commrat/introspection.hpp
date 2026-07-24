/**
 * @file introspection.hpp
 * @brief Main introspection header - include this for schema export
 * 
 * Provides complete introspection capabilities for CommRaT applications:
 * - CommRaTSchemaOutput: typed schema file format (extends sertial::SchemaOutputT)
 * - CommRaTMessageRecord: per-message record (layout + commrat metadata)
 * - IntrospectionHelper: registry-wide export to any rfl format
 * - MessageSchema: compile-time schema for a single type (full StructLayout)
 * 
 * **Quick Start:**
 * @code
 * #include <commrat/introspection.hpp>
 * 
 * using MyApp = CommRaT<Message::Data<TempData>, Message::Data<StatusData>>;
 * 
 * // Export all messages as CommRaTSchemaOutput JSON
 * auto json = MyApp::Introspection::export_all();
 * 
 * // Write to file (readable by sertial-inspect and commrat inspect)
 * MyApp::Introspection::write_to_file("schemas.json");
 * @endcode
 * 
 * @author CommRaT Development Team
 * @date February 12, 2026
 */

#pragma once

#include "commrat/introspection/commrat_schema_output.hpp"
#include "commrat/introspection/message_schema.hpp"
#include "commrat/introspection/introspection_helper.hpp"
