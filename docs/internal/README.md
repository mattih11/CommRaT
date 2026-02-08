# Internal Documentation

This directory contains design documents, architecture decisions, and development history for CommRaT contributors and maintainers.

**For users**: See [../USER_GUIDE.md](../USER_GUIDE.md) and [../ARCHITECTURE.md](../ARCHITECTURE.md) instead.

## Purpose

Internal documentation explains:
- **Why** technical decisions were made (not just what)
- **How** complex features were designed and evolved
- **What** challenges were encountered and solved
- **When** major changes happened (development history)

This knowledge helps future contributors understand the codebase and make informed decisions.

## Contents

### design_decisions/

Explains why specific technical choices were made:

- **RACK_ANALYSIS.md**: RACK-style getData mechanism for multi-input synchronization
- **IO_SYNC_STRATEGY.md**: Multi-input timestamp alignment and synchronization strategy
- **SERTIAL_RINGBUFFER_REQUEST.md**: Requirements for timestamped ring buffer implementation

### phase_history/

Development timeline and architectural evolution:

- **ARCHITECTURE_ANALYSIS.md**: Phase 5 I/O specifications design and multi-output analysis
- **FIXES_APPLIED.md**: Historical bug fixes, issues encountered, and solutions
- **Phase 6 Development**: Multi-input synchronization journey (Phases 6.1-6.10)

### refactoring/

Major refactoring documentation:

- **ARCHITECTURE_REFACTOR_PLAN.md**: Registry module extraction (1,952 → 1,003 lines, 49% reduction)

## Development Phases

CommRaT has evolved through several major phases:

### Phase 1-4: Foundation (Historical)
- Basic message passing
- Single mailbox architecture
- Simple module types

### Phase 5: I/O Specifications (Complete)
- `Output<T>` / `Outputs<Ts...>` for type-safe outputs
- `Input<T>` / `Inputs<Ts...>` for multi-input modules
- `PeriodicInput`, `LoopInput` as input modes
- Backward compatible normalization
- Helper base class pattern for conditional virtuals

### Phase 6: Multi-Input Synchronization (Complete - Phase 6.10)
- **6.1-6.2**: TimestampedRingBuffer with getData lookup
- **6.3**: HistoricalMailbox wrapper
- **6.4-6.5**: Module configuration and validation
- **6.6-6.7**: Subscription protocol for multi-input
- **6.8**: Multi-input compilation and type safety
- **6.9**: 3-input sensor fusion (IMU+GPS+Lidar)
- **6.10**: Timestamp metadata accessors (get_input_metadata, freshness tracking)

### Phase 7: Planned Future Work
- Optional secondary inputs (getData failure handling)
- Advanced buffering strategies
- ROS 2 adapter (separate repository)
- Performance profiling tools

## Key Design Principles

### 1. Compile-Time Validation
If something can be checked at compile time, it MUST be:
- Message type registration
- Message ID uniqueness
- Input/output type matching
- Template constraints via concepts

### 2. Zero-Allocation Mandate
No heap allocations in real-time paths:
- Use `std::array` and `sertial::fixed_vector`
- Pre-allocate all buffers
- No `std::vector::push_back()` in hot paths
- No dynamic polymorphism unless justified

### 3. Single Source of Truth
Each piece of data has one authoritative source:
- **Timestamps**: Only in `TimsHeader.timestamp` (no payload fields)
- **Message IDs**: Computed at compile time from type
- **Subscription state**: Managed by SubscriberManager
- **Metadata**: Populated once before process() calls

### 4. Blocking Receives, Not Polling
All message reception uses blocking I/O:
- 0% CPU when idle
- Deterministic wake-up on message arrival
- No busy-waiting loops
- Clean shutdown via stop flag

### 5. Three-Mailbox Architecture
Separate mailboxes for separate concerns:
- **CMD (offset +0)**: User commands
- **WORK (offset +16)**: Subscription protocol
- **DATA (offset +32)**: Input data streams

## Contributing

See [../CONTRIBUTING.md](../CONTRIBUTING.md) for:
- Code style guidelines
- Real-time programming constraints
- Template metaprogramming patterns
- Testing requirements
- Documentation requirements

## Questions?

If you're unsure where a document belongs:
- **User-facing concept?** → docs/USER_GUIDE.md
- **API usage?** → docs/API_REFERENCE.md
- **Design decision?** → docs/internal/design_decisions/
- **Development history?** → docs/internal/phase_history/
- **Refactoring notes?** → docs/internal/refactoring/

When in doubt, start with a document in internal/ and promote it if users need it.
