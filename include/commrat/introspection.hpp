/**
 * @file introspection.hpp
 * @brief Main introspection header - include this for schema export
 * 
 * Provides complete introspection capabilities for CommRaT applications:
 * - MessageSchema: Combines CommRaT metadata with SeRTial layout
 * - IntrospectionHelper: Registry-wide export to any rfl format
 * 
 * **Quick Start:**
 * @code
 * #include <commrat/introspection.hpp>
 * 
 * using MyApp = CommRaT<Message::Data<TempData>, Message::Data<StatusData>>;
 * 
 * // Export single message to JSON
 * auto json = MyApp::Introspection::export_as<TempData, rfl::json>();
 * 
 * // Export all messages to file
 * MyApp::Introspection::write_to_file<rfl::json>("schemas.json");
 * @endcode
 * 
 * @author CommRaT Development Team
 * @date February 12, 2026
 */

#pragma once

#include "commrat/introspection/message_schema.hpp"
#include "commrat/introspection/introspection_helper.hpp"

/**
 * @namespace commrat
 * @brief CommRaT introspection provides complete schema export
 * 
 * Exports both:
 * - CommRaT metadata (message IDs, type names, size bounds)
 * - SeRTial layout (fields, types, offsets, sizes, variable flags)
 * 
 * Supports any rfl format: JSON, YAML, TOML, XML, etc.
 */
