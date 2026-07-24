# CommRaT Development Roadmap

**Last Updated**: July 15, 2026

This document tracks planned features, improvements, and long-term ideas for CommRaT. For active bugs and runtime issues, see [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Current Focus

### Documentation Update (In Progress)
- Update USER_GUIDE.md, API_REFERENCE.md for Module2 API
- Fix outdated code examples throughout docs

## Near-Term Features

### API Improvements

**Manual Timestamp Override**
- Allow users to set custom timestamps for periodic outputs
- Use case: Replay from recorded data with original timestamps
- API: `set_output_timestamp()` or process overload
- Status: Planned
- Priority: Medium

**Optional Secondary Inputs** -- COMPLETED
- Implemented as `Synced<T>` wrapper with fresh/stale/invalid handling
- `SyncedInput<T>` I/O spec for secondary inputs

**Command Ergonomics**
- Improve command dispatch beyond overload-based pattern
- Consider visitor pattern or command handlers map
- Status: Design phase
- Priority: Low

### Memory Optimization

**Command Specification Wrapper**
- Wrap commands like `Command<ResetCmd>` via `DataWithCommands<T, Cmd1, ...>`
- Improves API consistency with I/O tuple architecture
- Status: Partially implemented
- Priority: Medium

**SendCommand Pattern**
- Compile-time validated command sending between modules
- Status: Planned
- Priority: Low

### Performance

**Input Buffering Strategies**
- Sliding window history (keep last N messages)
- Latest-only mode (single-element buffer)
- Time-based retention (keep messages within window)
- Status: Concept phase
- Priority: Medium

**RingBuffer Integration** -- COMPLETED
- `TimestampedRingBuffer` used in `ModuleOutput` for message history
- Zero-allocation circular buffer with compile-time sizing

## Long-Term Ideas

### External Integrations

**ROS 2 Adapter**
- Bridge CommRaT modules to ROS 2 ecosystem
- Bidirectional message translation
- Separate repository: `rclcpp-commrat`
- Status: Concept phase
- Priority: Low

**Real-Time Platform Abstraction Layer**
- Complete transition to platform wrappers (threading, timing, etc.)
- Enable compilation against libevl for hard real-time guarantees
- Swap platform layer without changing application code
- Duration type, free function constructors, std::/EVL backends
- `COMMRAT_PLATFORM` CMake cache variable selects backend (`STD` / `EVL`)
- Status: **COMPLETED** (std:: backend fully operational, EVL headers in place, implementation pending)
- Details: See `docs/work/PLATFORM_ABSTRACTION_LAYER.md`

**CI Pipeline** -- COMPLETED
- 3-job GitHub Actions workflow: `.github/workflows/ci.yml`
- `build-std`: builds and runs ctest in ratos-dev-image container (STD platform, CMake presets)
- `build-evl-compile`: compile-checks EVL platform build (`continue-on-error` until backends are implemented)
- `test-evl-runtime`: downloads RaTOS wic disk image via `dawidd6/action-download-artifact`, boots QEMU, rsync + build + ctest inside EVL kernel guest
- Image is public (no credentials required); only `RATOS_RELEASE_TOKEN` secret needed (artifact download)
- Scripts: `scripts/evl-dev.sh` — single script for all EVL dev work (`--cross`, `--build`, `--test`, `--run`, `--shell`)
- Configuration: `.commrat.env` (non-secret defaults, including `RATOS_RELEASE_TAG` pin), `.commrat.env.local` (machine-local overrides, gitignored)
- CMakePresets: `default` / `debug` / `evl` / `evl-cross` — used by CI, local builds, VS Code, and CLion
- Dev Container: `.devcontainer/devcontainer.json` — zero-setup IDE with full IntelliSense in ratos-dev-image

**Unified ModuleType Definition**
- Extend message registry to define complete module interfaces
- Link commands to output types: `ModuleType<DataT, CommandsT>`
- Modules can implement multiple types via `Outputs<Type1, Type2>`
- Enables stronger type contracts and automatic interface validation
- Status: Design phase
- Priority: Medium
- Details: See `docs/work/MODULE_TYPE_SYSTEM.md`

**System Lifecycle Commands**
- Extend SystemMessages for module lifecycle management
- All modules automatically respond to: on, off, reset
- Standardized lifecycle state machine
- Status: Planned
- Priority: High
- Details: See `docs/work/LIFECYCLE_SYSTEM.md`

**Parameter System**
- Strongly-typed parameter definition and loading
- Automatic parameter commands: get, set, list, save
- Load parameters on module start from config files
- Type-safe API with minimal boilerplate
- Status: Design phase
- Priority: High
- Details: See `docs/work/PARAMETER_SYSTEM.md`

**Sender Metadata Access**
- Add source/sender/requester info to `get_input_metadata<>()` helpers
- Enable filtering/validation based on message origin
- Useful for multi-source inputs and security
- Status: Planned
- Priority: Low

**Generic Algorithm Modules**
- Reusable modules for common operations: `WindowFilter<T>`, `KalmanFilter<T>`, `MovingAverage<T>`
- Work with any type supporting required arithmetic operations
- Separate repository: `commrat-algorithms`
- Status: Concept phase
- Priority: Low
- Details: See `docs/work/GENERIC_ALGORITHMS.md`

**Web-Based System Interface**
- Generic REST API with zero boilerplate
- WebSocket streaming for real-time data visualization
- Complete web-based GUI for system monitoring and control
- Separate repository: `commrat-webgui`
- Status: Concept phase
- Priority: Low
- Details: See `docs/work/WEB_INTERFACE.md`

## Open Architectural Questions

### Input Synchronization Policy
Current: Primary `Input<T>` drives execution; secondary `SyncedInput<T>` uses get_data.
Future: Consider `SynchronizedInputs<T1, T2, T3>` that waits for all inputs.

### Output Publishing Order
Current: Sequential (T1, T2, T3 in order).
Future: Consider priority-based publishing for critical outputs.

### Command Reply Timeout
Current: Blocking wait (system messages only).
Recommended: Configurable timeout with 1-second default.
Future: Async callback mechanism.

**DDS Compatibility Layer**
- Direct DDS backend instead of TiMS
- Enables interoperability with DDS-based systems
- Significant architectural effort
- Status: Research phase
- Priority: Low

### Developer Tools

**CommRaT Schema Viewer and `commrat_generate_schema()` CMake function** -- DONE
- `CommRaTMessageRecord { std::string layout; CommRaTMessageMetadata commrat; }` with
  `CommRaTSchemaOutput = sertial::SchemaOutputT<CommRaTMessageRecord>` (sertial extension)
- `IntrospectionHelper::export_all()` / `write_to_file()` emit `CommRaTSchemaOutput` JSON
- `commrat_generate_schema(TARGET APP_HEADER APP_TYPE OUTPUT)` CMake function in
  `cmake/CommRaTSchemaGen.cmake` (auto-included via `CommRaTConfig.cmake`)
- `tools/commrat-inspect/viewer.html` — interactive web viewer: memory layout, CommRaT
  metadata block, filter by name/ID, "hide system messages" checkbox
- GitHub Pages deployment (via `doxygen.yml`): live viewer at
  `https://<org>.github.io/<repo>/commrat-inspect/` with pre-built `AllExamplesApp` schema
- Format compatible with plain sertial `SchemaOutput` (CommRaT block optional)
- Status: Complete
- Priority: Done

**`commrat inspect` CLI subcommand**
- CLI tool: `commrat inspect <message_schemas.json>`
- Renders a human-readable table showing message IDs, type names, field layouts, sizes
- `--summary` flag: one-line-per-type table (id, name, size)
- `--message <name>` flag: full field breakdown for a specific type
- Status: Planned (web viewer in `tools/commrat-inspect/viewer.html` covers most use cases)
- Priority: Low

**Introspection output in `commrat_module()` descriptor (single source of truth)**
- Currently `*.module.json` only contains `module_class` + `binary` (CMake-static)
- Goal: `commrat_module()` CMake macro emits full schema into the descriptor via a
  post-build step — making `*.module.json` the single source of truth for types + IDs
- Design:
  1. Each module binary supports a `--commrat-inspect <outfile>` flag that calls
     `IntrospectionHelper::write_to_file()` and exits cleanly
  2. `commrat_module()` macro adds a CMake `add_custom_command(POST_BUILD ...)` that
     runs the binary with that flag, capturing `CommRaTSchemaOutput` into the descriptor:
     ```cmake
     add_custom_command(TARGET ${target} POST_BUILD
         COMMAND $<TARGET_FILE:${target}> --commrat-inspect
                 ${CMAKE_CURRENT_BINARY_DIR}/${class_name}.schema.json
         COMMENT "Generating CommRaT schema for ${class_name}")
     ```
  3. `CommRaTModuleDescriptor` (the `*.module.json` struct) gains a
     `std::optional<CommRaTSchemaOutput> schema` field — still readable by old loaders
  4. ProcessLauncher reads the embedded schema to verify message ID consistency
     before spawning (catches registry mismatch at launch time, not at runtime)
- Status: Design phase
- Priority: Low

**`AppDescription` and launch config as rfl-native structs**
- `module_description.hpp` structs (`AppDescription`, `ModuleDescription`, etc.) already
  work with `rfl::json::read<AppDescription>()` via reflectcpp aggregate reflection
- Missing: the header does not include `<rfl.hpp>`, relying on callers to provide it
- Todo: add `#include <rfl.hpp>` to `module_description.hpp` to make it self-contained
  and document the `rfl::json::read` / `rfl::yaml::read` contract explicitly
- Future: consider `rfl::Field<"name", T>` annotations to lock field names in JSON/YAML
  and support rename without breaking existing config files
- Status: Minor cleanup pending
- Priority: Low

**Performance Profiling**
- Built-in latency measurement
- Message rate tracking
- CPU usage per module
- Execution time profiling
- Status: Concept phase
- Priority: Medium

**Static Analysis for Real-Time Safety**
- Compile-time detection of allocations in hot paths
- Detect std::vector/std::string misuse
- Verify fixed-size container usage
- Status: Research phase
- Priority: Low

**Visual Module Inspector**
- Runtime visualization of module topology
- Message flow monitoring
- Subscription graph display
- Web-based dashboard
- Status: Concept phase
- Priority: Low

### Architecture

**Module Base Refactoring** -- COMPLETED
- Module2 I/O tuple architecture replaces old Module<> with 13+ base classes
- Clean `Module2<Output<T>, Input<U>, Period<D>>` API

**Async get_data**
- Non-blocking future-based historical data retrieval
- Use case: Complex fusion with optional inputs
- Status: Concept phase
- Priority: Low

### Quality of Life

**Adaptive Sync Tolerance**
- Dynamically adjust get_data tolerance based on message arrival rate
- Automatically tune for optimal synchronization
- Status: Concept phase
- Priority: Low

**History Persistence**
- Save/restore message buffers across module restarts
- Enables warm starts without re-priming
- Status: Concept phase
- Priority: Low

## Completed Features

For full history, see `docs/internal/phase_history/` and `docs/internal/refactoring/`.

- Module2 I/O tuple architecture (replaces old Module<>)
- `Synced<T>` wrapper for secondary inputs (fresh/stale/invalid)
- `TimestampedRingBuffer` in ModuleOutput
- TypedMailbox with compile-time buffer sizing (70-95% memory savings)
- Multi-input synchronization (primary/secondary with get_data)
- Multi-output modules (per-output CMD mailbox)
- Zero-copy workspace API
- Introspection system (JSON/YAML/TOML/XML export)
- Timestamp metadata accessors (`get_input_timestamp<N>()`, etc.)
- Subscription protocol integration (Subscribe/Unsubscribe/GetData dispatch in command_loop)
- User command reply mechanism (on_command reply sent via CMD mailbox)
- Platform abstraction layer (Duration type, Thread/Mutex/Time wrappers, std::/EVL backends)
- **Process launcher system** (`commrat_module()` CMake macro, `ProcessLauncher`, `MyApp::Launcher`; `AppDescription` YAML/JSON config drives fork/exec of per-module binaries; `--duration-ms` for CTest; descriptor auto-discovery from `*.module.json` files generated at build time)

## Contributing Ideas

Have a feature idea or improvement? Please:

1. Check if it's already listed here or in [KNOWN_ISSUES.md](KNOWN_ISSUES.md)
2. Open a GitHub issue with:
   - Clear use case description
   - Expected API or behavior
   - Why current approach doesn't work
3. For major architectural changes, propose design document in `docs/work/`

## Priority Definitions

- **High**: Addresses common user pain points or enables critical use cases
- **Medium**: Useful improvement but workarounds exist
- **Low**: Nice-to-have or specialized use case
