# Module Refactor: Quick Start

## What We're Building

Clean, tuple-based Module architecture with:
- Compile-time I/O tuple
- Integrated command association (DataWithCommands)
- Simple configuration
- Auto-generated mailboxes

## Current Status

✅ **Phase 0: Foundation Complete**
- DataWithCommands system
- CmdInput/ContinuousInput/SyncedInput classes (all implemented)
- Command extraction helpers

✅ **Phase 1.1: I/O Specification System Complete**
- I/O specification tags (Output<T>, Input<T>, Period<D>, SyncedInput<T>)
- Type traits (is_output_v, is_input_v, is_period_v, etc.)
- Execution mode validation (auto-infers Input/Period/Loop)
- CreateIOInstance mapper (tag → instance)
- BuildIOTuple builder (compile-time tuple construction)
- Consolidated io_spec.hpp (removed io_traits.hpp, io_wrappers.hpp duplicates)
- Index sequences (output_indices(), input_indices())
- Generic accessors (get_output<N>(), get_input<N>())

✅ **Phase 1.2: Module2 Integration Complete**
- Created Module2<> with I/O tuple architecture
- Compile-time index sequences (separate outputs from inputs)
- Clean accessor delegation to IOBuilder
- Threading architecture (1 data thread + N command threads)
- Lifecycle management (start/stop)
- Thread loops (command_loop, data_loop with mode dispatch)
- Address encoding integration (address_helpers.hpp)

✅ **Phase 1.3: Input Infrastructure Complete**
- Named input struct builder (reflect-cpp integration)
- get_inputs() returns struct with named fields
- Automatic duplicate handling (type_name_0, type_name_1)
- Moved to input_infrastructure.hpp (reusable mixin)

✅ **Phase 1.4: GetData Message Registration Complete**
- Automatic GetDataRequest/GetNextDataRequest creation for Message::Data<T>
- Unique message IDs via UserDefined::GetData/GetNextData subprefixes
- Same local_id as data message (different subprefix = no collision)
- Consumer-side SyncedInput::get_data() and get_next_data() implemented

✅ **Phase 1.5: Producer-Side GetData Handling Complete**
- ModuleOutput::handle_get_data_request() - timestamp-based buffer search
- ModuleOutput::handle_get_next_data_request() - latest message retrieval
- InterpolationMode support (NEAREST, BEFORE, AFTER)
- OutputBuffer integration with wrap-around handling
- All tests passing (test_module_output_getdata)

⏳ **Next: Phase 1.6 - Subscription Protocol Integration**
- ModuleOutput subscription handlers (Subscribe/Unsubscribe)
- WORK mailbox for subscription protocol
- Fix TypedMailbox compilation issues
- Continuous data streaming to subscribers
- Multi-output type filtering

## Example of Future Usage

```cpp
// Define data with commands
using SensorData = Message::DataWith<SensorPayload>::Commands<
    CalibrateCmd,
    SetRateCmd
>;

// Define module (clean!)
using SensorModule = Module<
    MyRegistry,
    Output<SensorData>,        // Produces sensor data (has commands)
    Input<PressureData>,       // Receives pressure data (input-driven)
    SyncedInput<IMUData>       // Synchronized IMU data (secondary)
>;

// OR timer-driven:
using PeriodicSensor = Module<
    MyRegistry,
    Output<SensorData>,
    Period<Milliseconds(100)>  // 100Hz periodic execution
>;

// OR loop-driven (auto-inferred - no Input, no Period):
using LoopSensor = Module<
    MyRegistry,
    Output<SensorData>         // Maximum throughput loop
>;

// Module automatically:
// - Creates ModuleOutput with command handlers
// - Creates ContinuousInput for pressure (if Input<T>)
// - Creates SyncedInput for IMU (if SyncedInput<T>)
// - Stores all in compile-time tuple
// - Generates correct process() signature
// - Infers execution mode (input/timer/loop-driven)
```

## Implementation Progress

### Week 1: I/O Tuple (COMPLETE)
- [x] I/O specification tags (Output<T>, Input<T>, Period<D>, SyncedInput<T>)
- [x] Type traits (is_output_v, is_input_v, is_period_v, etc.)
- [x] Execution mode validation (ValidateExecutionMode)
- [x] Instance creation mapper (CreateIOInstance)
- [x] Tuple builder (BuildIOTuple)
- [x] Consolidation (single io_spec.hpp file)
- [x] Module2<> integration (I/O tuple member)
- [x] I/O access methods (structured inputs()/outputs())
- [x] Zero-copy workspace API (ModuleOutput)
- [x] GetData protocol (consumer + producer)
- [ ] Subscription protocol (Subscribe/Unsubscribe)
- [ ] WORK mailbox integration
- [ ] Full integration tests

### Week 2: Subscription & Commands (IN PROGRESS)
- [ ] Fix TypedMailbox template issues
- [ ] Implement Subscribe/Unsubscribe handlers in ModuleOutput
- [ ] WORK mailbox integration
- [ ] Continuous data streaming to subscribers
- [ ] Registry expansion (ExpandAllDataWithCommands)
- [ ] Command handler generation in ModuleOutput
- [ ] Command dispatch in command_loop
- [ ] Integration tests for complete data flow

### Weeks 3-6
See MODULE_REFACTOR_STRATEGY.md for full plan

## Files Created/Modified

1. **docs/work/MODULE_REFACTOR_STRATEGY.md** - Full 6-week plan
2. **docs/work/COMMAND_ASSOCIATION.md** - Command system design
3. **docs/work/ZERO_COPY_ARCHITECTURE.md** - Zero-copy design (Phase 6 complete)
4. **docs/work/REGISTRY_CONSOLIDATION.md** - Registry cleanup and utilities plan
5. **include/commrat/messaging/data_with_commands.hpp** - Command association
6. **include/commrat/module/io/io_spec.hpp** - Complete I/O specification system
7. **include/commrat/module2.hpp** - Next-gen module with I/O tuple architecture
8. **include/commrat/module/io/output/module_output.hpp** - Zero-copy output with GetData handlers
9. **include/commrat/module/io/input/synced_input.hpp** - Pull-based input with get_data()
10. **include/commrat/module/io/synced.hpp** - Zero-copy wrapper for multi-input
11. **test/test_module_output_getdata.cpp** - GetData handler validation
12. **Deleted**: io_traits.hpp, io_wrappers.hpp (duplicates)

## Next Steps to Production-Ready Module2

**Phase 1.6: Subscription Protocol (Current)**
1. Fix TypedMailbox/WorkMailbox template instantiation issues
2. Implement ModuleOutput::handle_subscribe_request()
3. Implement ModuleOutput::handle_unsubscribe_request()
4. Add continuous_subscribers_ management
5. Integrate WORK mailbox for subscription replies
6. Test subscription flow: Subscribe → Ack → Continuous DATA stream

**Phase 1.7: Command Integration**
1. Fix CommandMailbox integration (currently commented out)
2. Implement command_loop() in Module2
3. Add command handler dispatch
4. Test command sending from consumer to producer
5. Validate DataWithCommands extraction and routing

**Phase 1.8: Module2 Production Readiness**
1. Integration tests (multi-input, multi-output, commands)
2. Performance validation (verify zero-copy guarantees)
3. Documentation (API reference, migration guide)
4. Replace Module with Module2 in commrat.hpp
5. Update all existing examples to Module2
6. Deprecate old Module class
