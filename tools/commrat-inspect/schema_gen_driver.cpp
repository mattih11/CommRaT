/// commrat_schema_gen_driver.cpp
///
/// Fixed source file compiled by commrat_generate_schema() with user-supplied
/// defines.  It includes the user's registry header and calls
/// IntrospectionHelper<AppType>::write_to_file() to emit a CommRaTSchemaOutput
/// JSON file containing CommRaT metadata + sertial layout for every message
/// type registered in the CommRaT<> registry.
///
/// Required compile definitions
/// -----------------------------
///   COMMRAT_APP_HEADER   Quoted path to the header that defines the CommRaT<>
///                        registry alias, e.g.:
///                          "/src/myapp/include/myapp/all_messages.hpp"
///                        which contains something like:
///                          using MyApp = commrat::CommRaT<
///                              commrat::Message::Data<TempData>,
///                              commrat::Message::Data<StatusData>
///                          >;
///
///   COMMRAT_APP_TYPE     C++ type name of the CommRaT<> registry alias, e.g.:
///                          MyApp
///                        Use the fully qualified name if the alias is in a
///                        namespace, e.g. foo::MyApp.
///
///   COMMRAT_OUTPUT_FILE  Absolute path for the generated JSON schema file.
///
/// Output format
/// -------------
/// CommRaTSchemaOutput (sertial::SchemaOutputT<CommRaTMessageRecord>):
///   { "version": "5.1.0", "generated": "...", "messages": [
///       { "layout": "{ ...sertial StructLayout JSON... }",
///         "commrat": { "message_id": 1, "payload_type": "TempData", ... } },
///       ...
///   ]}
///
/// This is a superset of sertial SchemaOutput: sertial-inspect reads only
/// the "layout" field and ignores "commrat".  commrat-inspect reads both.

#include <commrat/meta/introspection_helper.hpp>
#include <iostream>

#ifndef COMMRAT_APP_HEADER
#  error "COMMRAT_APP_HEADER must be defined — quoted path to the header containing the CommRaT<> registry alias"
#endif
#ifndef COMMRAT_APP_TYPE
#  error "COMMRAT_APP_TYPE must be defined — C++ type name of the CommRaT<> registry (e.g. MyApp)"
#endif
#ifndef COMMRAT_OUTPUT_FILE
#  error "COMMRAT_OUTPUT_FILE must be defined — absolute output path for the generated JSON schema file"
#endif

// Pull in the user's registry header.
#include COMMRAT_APP_HEADER

int main() {
    // IntrospectionHelper<AppType> already knows:
    //   - every payload type in the registry (via AppType::PayloadTypes)
    //   - each type's message ID (via AppType::get_message_id<T>())
    //   - each wire layout (via sertial::export_layout_data<WireMessage<T>>())
    // It writes CommRaTSchemaOutput JSON directly — no extra configuration needed.
    try {
        COMMRAT_APP_TYPE::Introspection::write_to_file(COMMRAT_OUTPUT_FILE);
        std::cout << "commrat-schema-gen: wrote schema to " COMMRAT_OUTPUT_FILE "\n";
    } catch (const std::exception& e) {
        std::cerr << "commrat-schema-gen: error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
