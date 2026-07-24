# CommRaT Schema Viewer

Interactive HTML viewer for CommRaT schema JSON files.
Extends the sertial-inspect viewer with CommRaT-specific metadata:
message IDs, registry names, wire type names, and a "hide system messages"
filter.

## Live viewer (GitHub Pages)

Open the pre-built AllExamplesApp schema directly in your browser:

```
https://<org>.github.io/<repo>/commrat-inspect/viewer.html?schema=all_examples_schema.json
```

Or with an explicit raw GitHub URL (works from any browser, no clone needed):

```
https://<org>.github.io/<repo>/commrat-inspect/viewer.html?schema=https://raw.githubusercontent.com/<org>/<repo>/main/path/to/schema.json
```

## Local usage

Open `viewer.html` directly in a browser and use the file picker to load a
`CommRaTSchemaOutput` JSON file, or pass a relative or absolute URL:

```
viewer.html?schema=../../build/default/examples/all_examples_schema.json
```

Serve locally to load cross-origin schemas:

```bash
cd build/default/examples
python3 -m http.server 8080
# then open: http://localhost:8080/../../tools/commrat-inspect/viewer.html?schema=http://localhost:8080/all_examples_schema.json
```

## Generating a schema JSON

### CMake (recommended — runs automatically on every build)

```cmake
commrat_generate_schema(
    TARGET      my_module
    APP_HEADER  "${CMAKE_SOURCE_DIR}/include/myapp/my_app.hpp"
    APP_TYPE    MyApp
    OUTPUT      "${CMAKE_BINARY_DIR}/my_app_schema.json"
)
```

`commrat_generate_schema()` creates a `<TARGET>_schema_gen` side-car
executable and attaches a `POST_BUILD` command so the JSON is regenerated
whenever the registry header changes.  It is available automatically after
`find_package(CommRaT REQUIRED)`.

### C++ (runtime export)

```cpp
#include <commrat/introspection.hpp>

// Write CommRaTSchemaOutput JSON to file
MyApp::Introspection::write_to_file("my_app_schema.json");

// Or get the JSON as a string
std::string json = MyApp::Introspection::export_all();
```

## Input formats

The viewer accepts both CommRaT and plain sertial output files:

| Format | `messages[i]` element | CommRaT block shown? |
|---|---|---|
| `CommRaTSchemaOutput` (CommRaT) | `{layout: string, commrat: {...}}` | Yes |
| `SchemaOutput` (sertial) | JSON string of `StructLayout` | No (layout only) |

## Features

- Message ID in decimal and hex per type
- Wire type name (`corerat::WireMessage<T>`)
- Registry name (abbreviated)
- Filter bar: by type name or message ID
- "Hide system messages" checkbox (suppresses `commrat::Subscribe/...`)
- Interactive memory layout with byte-level visualization
- Variable-size field animation
- Cross-compatible with `sertial-inspect` output files
