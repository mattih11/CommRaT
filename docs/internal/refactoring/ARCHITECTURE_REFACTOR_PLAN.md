# CommRaT Architecture Refactor Plan

**Date**: February 8, 2026  
**Status**: ✅ **PHASES 1-5 COMPLETE** (Wave 6 cleanup done)  
**Goal**: Reorganize codebase for better maintainability and Phase 7 readiness

## Current State (Post-Phase 5 Modularization)

### File Size Overview (Current)
```
registry_module.hpp:  1003 lines (down from 1952!) ✅ 49% REDUCTION
io_spec.hpp:          562 lines
message_registry.hpp: 552 lines
timestamped_ring_buffer.hpp: 432 lines
threading.hpp:        386 lines
historical_mailbox.hpp: 324 lines
timestamp.hpp:        297 lines
registry_mailbox.hpp: 249 lines
messages.hpp:         237 lines
message_id.hpp:       160 lines
tims_wrapper.hpp:     136 lines
commrat.hpp:          115 lines
system_registry.hpp:  96 lines
message_helpers.hpp:  92 lines
subscription_messages.hpp: 86 lines
```

### Achieved Progress ✅

**Phase 1**: Directory Structure Created
- ✅ `messaging/`, `mailbox/`, `module/`, `platform/` directories
- ✅ Clean separation of concerns

**Phase 2**: Platform Layer Extracted
- ✅ `platform/threading.hpp` - Thread, Mutex abstractions
- ✅ `platform/timestamp.hpp` - Time, Duration abstractions
- ✅ `platform/tims_wrapper.hpp` - TiMS C API wrapper

**Phase 3**: Messaging System Extracted
- ✅ `messaging/message_id.hpp` - Message ID structure
- ✅ `messaging/message_registry.hpp` - Compile-time registry
- ✅ `messaging/message_helpers.hpp` - Message::Data<T>, Message::Command<T>
- ✅ `messaging/system/subscription_messages.hpp` - Subscription protocol
- ✅ `messaging/system/system_registry.hpp` - System message registry

**Phase 4**: Mailbox Layer Extracted
- ✅ `mailbox/mailbox.hpp` - Base mailbox class
- ✅ `mailbox/registry_mailbox.hpp` - Registry wrapper
- ✅ `mailbox/historical_mailbox.hpp` - With getData support
- ✅ `mailbox/timestamped_ring_buffer.hpp` - Timestamped buffer

**Phase 5**: registry_module.hpp Modularization
- ✅ `module/module_config.hpp` - Configuration structs
- ✅ `module/io_spec.hpp` - I/O specifications
- ✅ `module/traits/type_extraction.hpp` - Input/output extraction
- ✅ `module/traits/processor_bases.hpp` - Helper base classes
- ✅ `module/traits/multi_input_resolver.hpp` - Multi-input resolution
- ✅ `module/helpers/address_helpers.hpp` - Address calculation
- ✅ `module/helpers/tims_helpers.hpp` - TiMS message helpers
- ✅ `module/metadata/input_metadata.hpp` - Metadata structures
- ✅ `module/metadata/input_metadata_accessors.hpp` - Accessor mixin
- ✅ `module/subscription.hpp` - SubscriberManager mixin (396 lines)
- ✅ `module/publishing.hpp` - Publishing logic (~200 lines)
- ✅ `module/loops/loop_executor.hpp` - Loop implementations (~250 lines)

**Phase 5 Wave 6**: Cleanup
- ✅ Attempted extraction of remaining stateful helpers
- ✅ Determined complex helpers better kept inline
- ✅ Removed unnecessary abstraction files
- ✅ Inlined simple mailbox config creation
- ✅ **Final result: registry_module.hpp at 1,003 lines (49% reduction)**

### Current Structure ✅

### Current Structure ✅

```
include/commrat/
├── commrat.hpp                    # Main user-facing API
│
├── messaging/                     # Message system ✅ EXTRACTED
│   ├── message_id.hpp
│   ├── message_registry.hpp
│   ├── message_helpers.hpp
│   └── system/
│       ├── subscription_messages.hpp
│       └── system_registry.hpp
│
├── mailbox/                       # Mailbox layer ✅ EXTRACTED
│   ├── mailbox.hpp
│   ├── registry_mailbox.hpp
│   ├── historical_mailbox.hpp
│   └── timestamped_ring_buffer.hpp
│
├── module/                        # Module framework ✅ MODULARIZED
│   ├── module_config.hpp          # Configuration
│   ├── io_spec.hpp                # I/O specifications
│   ├── subscription.hpp           # SubscriberManager mixin (396 lines)
│   ├── publishing.hpp             # Publishing logic (~200 lines)
│   ├── traits/                    # Type analysis
│   │   ├── type_extraction.hpp
│   │   ├── processor_bases.hpp
│   │   └── multi_input_resolver.hpp
│   ├── helpers/                   # Helper utilities
│   │   ├── address_helpers.hpp
│   │   └── tims_helpers.hpp
│   ├── metadata/                  # Input metadata (Phase 6.10)
│   │   ├── input_metadata.hpp
│   │   └── input_metadata_accessors.hpp
│   └── loops/                     # Loop implementations
│       └── loop_executor.hpp      # CRTP mixin (~250 lines)
│
├── platform/                      # Platform abstractions ✅ EXTRACTED
│   ├── threading.hpp
│   ├── timestamp.hpp
│   └── tims_wrapper.hpp
│
├── messages.hpp                   # Core message types
└── registry_module.hpp            # Module base class (1,003 lines)
```

### What Remains in registry_module.hpp (1,003 lines)

**Core module implementation:**
- Module class template definition
- Member variables (config, mailboxes, threads, metadata)
- Constructor/destructor with mailbox initialization
- Lifecycle methods (start, stop, on_init, on_start, on_stop, on_cleanup)
- Subscription protocol handlers
- Command/work/data loop methods
- Multi-input synchronization helpers (tightly coupled to state)
- Helper methods for mailbox/multi-input initialization

**Why these remain inline:**
- Tightly coupled to module state (config_, mailboxes_, metadata_)
- Frequent access to member variables
- Complex coordination between components
- CRTP mixins need complete Module type
- Extraction would require passing many parameters

## Lessons Learned from Wave 6

### What Worked ✅
- **CRTP mixins for cohesive features**: LoopExecutor, InputMetadataAccessors, SubscriberManager
- **Standalone utilities**: address_helpers, tims_helpers
- **Static helpers**: Mailbox config creation (inlined back for simplicity)

### What Didn't Work ❌
- **Free function helpers**: Required too many parameters, lost type safety
- **Over-abstraction**: Helper files for 2-3 simple functions not worth ceremony
- **Stateful helpers**: Multi-input sync logic too coupled to module state

### Best Practices Identified
1. **Extract when feature is cohesive** (subscription, publishing, loops)
2. **Use CRTP for stateful mixins** (needs module context but separable)
3. **Keep tightly coupled code together** (multi-input helpers)
4. **Inline simple utilities** (mailbox config creation)
5. **Don't create files for <100 lines** unless very distinct concern

## Remaining Opportunities

### Potential Future Extractions

**Low Priority** (current structure good enough):
- Command dispatch logic → `module/commands/command_dispatcher.hpp`
- Work loop handlers → `module/subscription/subscription_handlers.hpp`
- Multi-input helpers → keep inline (too stateful)

**For Phase 7**:
- Optional input handling → `module/traits/optional_input_resolver.hpp`
- Buffering strategies → `mailbox/buffering/` directory
- Performance profiling → `module/profiling/` directory

## Metrics

### Line Count Reduction
- **Original**: registry_module.hpp = 1,952 lines
- **Current**: registry_module.hpp = 1,003 lines
- **Extracted**: ~949 lines to separate modules
- **Reduction**: 49% (achieved goal of <1,100 lines)

### Files Created (Phase 5)
- ✅ 15 new modular files
- ✅ Clear responsibility per file
- ✅ All <400 lines each
- ✅ Clean includes and dependencies

## Success Criteria ✅

- ✅ All tests pass (`make run_tests`)
- ✅ User-facing API unchanged
- ✅ registry_module.hpp <1,100 lines (achieved: 1,003)
- ✅ Clear directory structure
- ✅ Documentation updated
- ✅ No unnecessary abstraction files

## Timeline Actual

- **Phase 1**: Directory structure (1 hour)
- **Phase 2**: Platform layer (30 min)
- **Phase 3**: Messaging layer (1 hour)
- **Phase 4**: Mailbox layer (1.5 hours)
- **Phase 5 Waves 1-5**: Module extraction (6 hours)
- **Phase 5 Wave 6**: Cleanup attempt and revert (2 hours)

**Total**: ~12 hours over 2 days (Feb 7-8, 2026)

## Status Summary

### Completed ✅
- ✅ **Phases 1-4**: Infrastructure layers extracted
- ✅ **Phase 5 Waves 1-5**: Major module features extracted (subscription, publishing, loops, metadata)
- ✅ **Phase 5 Wave 6**: Cleanup and pragmatic decisions on what to keep inline
- ✅ **49% reduction** in registry_module.hpp size
- ✅ **All tests passing** - no functionality lost
- ✅ **Architecture ready for Phase 7**

### Not Pursued (Intentional) ❌
- Further helper extraction (determined unnecessary)
- Free function abstractions (lost type safety)
- Over-modularization of simple logic

---

## Next Steps: Phase 7 Development

With clean architecture in place, ready for:

1. **Phase 7.1**: Optional secondary inputs
   - Add to `module/traits/optional_input_resolver.hpp`
   - Extend LoopExecutor with fallback logic

2. **Phase 7.2**: Input buffering strategies
   - Add `mailbox/buffering/` directory
   - Sliding window, latest-only modes

3. **Phase 7.3**: ROS 2 adapter (separate repo)
   - Bridge to rclcpp
   - Message conversion utilities

4. **Phase 7.4**: Performance profiling
   - Add `module/profiling/` directory
   - Latency measurement, RT validation

**Architecture is solid - time to build features!** 🚀
