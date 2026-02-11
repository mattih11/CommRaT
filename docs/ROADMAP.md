# CommRaT Development Roadmap

**Last Updated**: February 11, 2026

This document tracks planned features, improvements, and long-term ideas for CommRaT. For active bugs and runtime issues, see [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Current Focus

### Documentation (In Progress)
- Complete USER_GUIDE.md sections
- Create runnable examples (hello_commrat, sensor_fusion, etc.)
- API reference with cross-references
- Architecture diagrams and explanations

## Near-Term Features

### API Improvements

**Manual Timestamp Override**
- Allow users to set custom timestamps for periodic outputs
- Use case: Replay from recorded data with original timestamps
- API: `set_output_timestamp()` or process overload
- Status: Planned
- Priority: Medium

**Optional Secondary Inputs**
- Make synchronized inputs return `std::optional<const T&>`
- Allows graceful handling when getData fails (tolerance exceeded)
- Avoids using stale data without freshness checks
- Status: Planned
- Priority: High

**Command Ergonomics**
- Improve command dispatch beyond overload-based pattern
- Consider visitor pattern or command handlers map
- Status: Design phase
- Priority: Low

### Memory Optimization

**Command Specification Wrapper**
- Wrap commands like `Command<ResetCmd>` similar to `Input<T>`/`Output<T>`
- Improves API consistency
- Enables better type introspection
- Status: Detailed design exists (see work/COMMAND_AND_MAILBOX_REFACTOR_PLAN.md)
- Priority: Medium

**SendCommand Pattern**
- Enable compile-time validated command sending between modules
- Type-safe inter-module command routing
- Detailed design exists
- Status: Planned (see work/COMMAND_AND_MAILBOX_REFACTOR_PLAN.md)
- Priority: Low

### Performance

**Input Buffering Strategies**
- Sliding window history (keep last N messages)
- Latest-only mode (single-element buffer)
- Time-based retention (keep messages within window)
- Status: Concept phase
- Priority: Medium

**RingBuffer Zero-Copy Integration**
- Use SeRTial RingBuffer for message history
- Zero-allocation circular buffer with serialization
- Already available in SeRTial, needs integration
- Status: Requirements documented (see work/SERTIAL_RINGBUFFER_REQUEST.md)
- Priority: Low

## Long-Term Ideas

### External Integrations

**ROS 2 Adapter**
- Bridge CommRaT modules to ROS 2 ecosystem
- Bidirectional message translation
- Separate repository: `rclcpp-commrat`
- Status: Concept phase
- Priority: Low

**DDS Compatibility Layer**
- Direct DDS backend instead of TiMS
- Enables interoperability with DDS-based systems
- Significant architectural effort
- Status: Research phase
- Priority: Low

### Developer Tools

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

**Module Base Refactoring**
- Reduce Module class from 13+ base classes to specialized variants
- Eliminate `void/void` template specialization workarounds
- Cleaner error messages and faster compilation
- Status: Detailed proposal exists (see work/MODULE_BASE_REFACTORING_PROPOSAL.md)
- Priority: Low (current architecture works)
- Risk: High (major refactoring)

**Async getData**
- Non-blocking future-based historical data retrieval
- Use case: Complex fusion with optional inputs
- Status: Concept phase
- Priority: Low

### Quality of Life

**Adaptive Sync Tolerance**
- Dynamically adjust getData tolerance based on message arrival rate
- Automatically tune for optimal synchronization
- Status: Concept phase
- Priority: Low

**History Persistence**
- Save/restore message buffers across module restarts
- Enables warm starts without re-priming
- Status: Concept phase
- Priority: Low

## Completed Features

For historical context on completed work, see `docs/internal/phase_history/` and `docs/internal/refactoring/`.

### Recent Completions

**Type-Specific Mailbox Sizing** (Complete)
- Mailboxes sized based on actual message types they handle
- 70-95% memory reduction achieved
- Compile-time type validation for send/receive
- `TypedMailbox<Registry, AllowedTypes...>` with static_assert checking
- See `docs/internal/refactoring/` for implementation details

**Registry Module Refactoring** (Complete)
- Extracted type traits, multi-output, threading, protocol loops
- Reduced registry_module.hpp from 1437 lines to manageable components
- Improved code organization and maintainability
- See `docs/internal/refactoring/REGISTRY_MODULE_REFACTORING_COMPLETE.md`

**Multi-Input Synchronization** (Complete)
- Primary/secondary input pattern
- HistoricalMailbox with getData mechanism
- Timestamp-based synchronization with tolerance
- 100% success rate in sensor fusion testing

**Multi-Output Modules** (Complete)
- Type-specific message publishing
- Per-output MailboxSet architecture
- Automatic type filtering for subscribers

**Timestamp Metadata Accessors** (Complete)
- `get_input_timestamp<N>()`
- `get_input_metadata<N>()`
- `has_new_data<N>()`
- `is_input_valid<N>()`

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
