# SeRTial: Expose typed `MessageLayoutRecord` for downstream composition

## Context

CommRaT uses SeRTial for serialization and builds on top of `SchemaGenerator` /
`SchemaOutput` for its `commrat inspect` developer tool.  CommRaT needs to emit
per-message records that contain **both** the SeRTial layout data **and**
CommRaT-specific metadata (message ID, registry name, etc.).

The goal is to allow CommRaT to compose the SeRTial record via `rfl::Flatten` so
that:

- `sertial-inspect` can still read CommRaT schema files (the SeRTial fields are
  in the same place, the CommRaT block is an additional field it can ignore)
- CommRaT does **not** duplicate the SeRTial schema logic
- The composition is expressed in C++ types, not string-level JSON merging

---

## Current API (the problem)

```cpp
// sertial/integration/schema_generator.hpp
struct SchemaOutput {
    std::string version;
    std::string generated;
    std::vector<std::string> messages;  // <-- opaque JSON blobs
};
```

Each element of `messages` is a raw JSON string produced by
`export_layout_data<T>()`.  There is no named C++ type for a single message
record, so downstream code cannot:

1. Add fields alongside the layout data via `rfl::Flatten`
2. Deserialize the record back into a typed struct
3. Extend `SchemaOutput.messages` with a different element type

---

## Requested change

Introduce a named struct for the per-message record and expose it as part of
the public API.  `SchemaOutput` becomes a template so callers can supply a
custom record type that extends the base one.

### Minimal addition (no breaking change)

```cpp
namespace sertial {

/// A single entry in a schema output file.
/// Wraps the serialized StructLayout data for one type.
/// rfl-reflectable so downstream can compose via rfl::Flatten.
struct MessageLayoutRecord {
    std::string layout;  // rfl::json::write(StructLayout<T>{})
};

/// Schema collection output format (existing — unchanged for v1).
/// Existing users keep std::vector<std::string>; new users can use
/// SchemaOutputT<MessageLayoutRecord> or their own extension.
struct SchemaOutput {
    std::string version   = "5.1.0";
    std::string generated;
    std::vector<std::string> messages;  // keep for backward compat
};

/// Typed variant — preferred for new tooling.
template<typename Record = MessageLayoutRecord>
struct SchemaOutputT {
    std::string version   = "5.1.0";
    std::string generated;
    std::vector<Record> messages;
};

} // namespace sertial
```

`SchemaGenerator` gains a parallel `generate_typed_data<Record>()` method that
populates `SchemaOutputT<Record>`.  The existing `generate_data()` method is
unchanged.

---

## How CommRaT uses this

```cpp
// commrat/introspection/commrat_schema_output.hpp

#include <sertial/integration/schema_generator.hpp>
#include <rfl.hpp>

namespace commrat {

struct CommRaTMessageRecord {
    rfl::Flatten<sertial::MessageLayoutRecord> sertial;  // layout field in-place
    MessageSchema::CommRaTMetadata commrat;               // message_id, type names, etc.
};

using CommRaTSchemaOutput = sertial::SchemaOutputT<CommRaTMessageRecord>;

// IntrospectionHelper writes CommRaTSchemaOutput to file.
// sertial-inspect reads CommRaTSchemaOutput: the "sertial.layout" field is
// identical to what it already knows; the "commrat" block is extra.
// commrat inspect reads the "commrat" block for message IDs and routing info.

} // namespace commrat
```

The JSON output looks like:

```json
{
  "version": "5.1.0",
  "generated": "...",
  "messages": [
    {
      "layout": "{ ... StructLayout fields ... }",
      "commrat": {
        "message_id": 1,
        "payload_type": "TemperatureData",
        "full_type": "TimsMessage<TemperatureData>",
        "max_message_size": 4096,
        "registry_name": "MyApp"
      }
    }
  ]
}
```

`sertial-inspect` only reads `layout`; `commrat inspect` reads both.

---

## What we are NOT asking for

- No changes to `StructLayout<T>` itself
- No changes to `export_layout_data()` or `export_schema()`
- No changes to the existing `SchemaOutput` / `SchemaGenerator` public API
  (this is purely additive)
- No dependency on CommRaT inside SeRTial

---

## Acceptance criteria

1. `sertial::MessageLayoutRecord` is a public struct with at least a `layout`
   field of type `std::string` and is rfl-reflectable (aggregate or explicit
   reflector)
2. `sertial::SchemaOutputT<Record>` is a public template with `version`,
   `generated`, and `messages: vector<Record>` fields; rfl-reflectable
3. `SchemaGenerator<Collection>::generate_typed_data<Record>()` returns
   `SchemaOutputT<Record>` where `Record` defaults to `MessageLayoutRecord`
4. Existing `SchemaOutput` and `SchemaGenerator::generate_data()` are
   unchanged (zero breaking changes)
5. All existing SeRTial tests pass
