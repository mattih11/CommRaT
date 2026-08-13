# CommRaT Known Issues

This document tracks known issues, limitations, and areas requiring investigation in CommRaT.

**Last Updated**: August 10, 2026

---

## Active Issues

### -1. EVL Tests Hang — CoreRaT `gettid()` Called from OOB Thread (CoreRaT Bug)

**Status**: Active — root cause confirmed, fix required in CoreRaT  
**Priority**: High  
**Affects**: All EVL runtime tests that instantiate `Module2` with outputs

**Root Cause**:
`corerat::evl_detail::attach_this_thread()` in
`corerat/ipc/evl/evl_backend.hpp` is called at the top of every
`EvlMailbox::receive_impl()`. It calls `gettid()`, which is an in-band kernel
syscall, from an already-attached EVL OOB thread:

```cpp
inline void attach_this_thread() noexcept {
    const int r = evl_attach_thread(EVL_CLONE_PRIVATE,
                                    "corerat.%d",
                                    static_cast<int>(gettid()));  // BUG: in-band syscall from OOB
}
```

This triggers `EVL_HMDIAG_SYSDEMOTE` (cause=2), which sends `SIGXCPU`
(`SIGDEBUG = SIGXCPU` in libevl) to the offending thread. Because `evl_init()`
installs a non-terminating `SIGXCPU` handler, the thread does not die but enters
a broken state, causing `module.stop()` to hang indefinitely in `thread.join()`.

**Call chain confirmed via GDB backtrace on EVL guest (RaTOS SDK v0.2.6)**:
```
Thread 2 (command thread, already OOB-attached via Thread::thread_entry):
  gettid()                                       <- in-band syscall from OOB
  corerat::EvlMailbox::receive_impl(...)
  commrat::Module2::command_loop_impl<0>()
  corerat::Thread::thread_entry(void*)
```

**Affected tests** (all hang or die with SIGXCPU on EVL guest):
- `test_3input_fusion`, `test_timestamp_logic`, `test_address_collisions`
- `example_continuous_input`, `example_loop_mode` (via `run_continuous_example.sh`)
- Any test/example that starts a `Module2` with at least one output

**Workaround**: None within CommRaT. Fix must be in CoreRaT.

**Fix for CoreRaT** (options):
1. Cache `gettid()` result before `evl_attach_self()` in `Thread::thread_entry()`,
   so `attach_this_thread()` does not need to call it.
2. Remove `attach_this_thread()` from the hot `receive_impl()` path — all
   CommRaT threads are already attached; non-EVL callers should attach once
   before the first OOB call, not on every receive.
3. Use `evl_get_self() >= 0` as an already-attached check (OOB-safe) to skip
   the `gettid()` branch entirely.

### 0. TiMS Router Resource Limit Under Stress

**Status**: Known Limitation  
**Priority**: Low

The TiMS router (`tims_router_tcp` from RACK) has a connection/mailbox limit. The CoreRaT router (`corerat-router-tcp`) does not have this issue. Creating 10+ concurrent modules in a single process with the old RACK router causes it to drop connections, resulting in:
```
Tims: recv head ERROR, (Connection reset by peer)
Failed to start PUBLISH mailbox: TiMS initialization failed
```

**Affected**: `test_address_collisions` Test 8 (stress test with 10 instances)  
**Not affected**: Normal usage with ~5-6 concurrent modules  
**Workaround**: Use `corerat-router-tcp` (no connection limit) or restart `tims_router_tcp` between stress tests.
**Root cause**: TiMS router connection table finite; not a CommRaT code issue.

### 1. Type-Based Metadata Access Limited to 2 Types

**Status**: By Design (Current Implementation)  
**Priority**: Low

Index-based metadata access `get_input_metadata<0>()` is the supported API. Type-based access was removed with the old metadata/ infrastructure.

### 2. Subscription Protocol Not Yet Wired (RESOLVED)

**Status**: Resolved  
**Priority**: N/A

The subscription protocol IS fully wired in `Module2::command_loop_impl()`. Each output CMD mailbox dispatches to `visit_system_commands()` which handles SubscribeRequest, UnsubscribeRequest, GetDataRequest, and GetNextDataRequest via `CommandHandler::handle_system_command()`. Consumer-side `ContinuousInput::subscribe()` sends SubscribeRequest and waits for reply. Producer-side `ModuleOutput::publish()` sends data to all subscribers. End-to-end flow proven by `test_3input_fusion`.

The old `subscription.hpp` (353 lines, legacy class with RT violations) was dead code not used by Module2 and has been removed.

### 3. No User Command Reply Mechanism (RESOLVED)

**Status**: Resolved  
**Priority**: N/A

User command replies are now sent via CMD mailbox. `CommandHandler::visit_user_command()` calls `derived->on_command<OutputIndex>(cmd, reply)` and sends the reply via `cmd_mailbox.send_reply()`, matching the system command pattern exactly.

---

## Resolved Issues

### 1. Multi-Input get_data Synchronization (RESOLVED - High Priority)

**Status**: Resolved (commit 519469b)  
**Affected**: Example 03 (Multi-Input Fusion), test_multi_input_module  
**Resolved**: February 8, 2026

**Root Cause**:
- **CRITICAL BUG**: Tolerance unit conversion missing in `timestamped_ring_buffer.hpp` line 295
- Timestamps from `Time::now()` are `uint64_t` nanoseconds since epoch
- Tolerance parameter is `Duration` (milliseconds from config)
- Code cast `tolerance.count()` directly to `uint64_t` without converting ms → ns
- With 100ms tolerance config → became 100 **nanoseconds** (100,000x too small!)

**Impact**:
- GPS at 10Hz: 100ms spacing = 100,000,000 ns between messages
- IMU at 100Hz: 10ms spacing = 10,000,000 ns between messages
- Even perfect timing has >10ms difference, but tolerance was 0.0001ms
- **Result**: All get_data queries failed with "no matching message within tolerance"

**Solution** (commit 519469b):
```cpp
// BEFORE (WRONG):
uint64_t tolerance_units = static_cast<uint64_t>(tolerance.count());

// AFTER (CORRECT):
// Convert tolerance from milliseconds to nanoseconds
// Timestamps are in nanoseconds (from Time::now()), so tolerance must match
uint64_t tolerance_ns = static_cast<uint64_t>(tolerance.count()) * 1'000'000ULL;
```

**Verification**:
- Example 03 now shows successful fusion: `[Fusion] #100 | GPS: fresh age=17.7ms`
- Sync ages well within 100ms tolerance: 17ms, 36ms, 66ms, 87ms, 7ms, 27ms
- Monitor receives continuous stream of fused outputs
- Phase 6.9 Multi-Input Synchronization feature **now fully functional**

**Lessons Learned**:
- Unit mismatches (ms vs ns) are subtle but catastrophic bugs
- Comments like "unit depends on usage" are dangerous - should be explicit
- Parameter naming matters: `tolerance_units` was ambiguous, `tolerance_ns` is clear
- Testing with realistic data rates is crucial to catch magnitude errors

---

### 2. Output<void> Pattern (RESOLVED)

**Resolution**: Use pass-through pattern `Output<T>` returning input type instead of `Output<void>`. Example 01 demonstrates correct pattern.

### 3. AutoAssignIDs Dropped Command Subprefix and Reply Type (RESOLVED)

**Status**: Resolved (April 3, 2026)  
**Affected**: All `Message::Command<>` types in registries with auto-assigned IDs

**Root Cause**: Two bugs in `message_registry.hpp`:
1. `MessageDefinition::subprefix` lambda had no `uint8_t` passthrough case. When `AutoAssignIDsProcess` re-instantiated a message with `First::subprefix` (already `uint8_t`), the `else` branch returned `UserSubPrefix::Data = 0`, silently reclassifying all Command messages as Data.
2. `CurrentProcessed` in `AutoAssignIDsProcess` only passed 4 template args to `MessageDefinition`, dropping `ReplyPayload`. Command messages lost their reply type, so `ExpandReplies` generated no reply messages.

**Symptoms**: `registry::command_messages_t<MyApp>` returned 0 messages; `registry::data_messages_t<MyApp>` returned extra entries.

**Fix** (`include/commrat/messaging/message_id.hpp`):
Added `uint8_t` direct-passthrough at top of subprefix lambda.

**Fix** (`include/commrat/messaging/message_registry.hpp`):
Added `GetReplyPayload<First>` helper to safely extract reply type (defaulting to void when absent), passed it as 5th arg to `MessageDefinition` in `CurrentProcessed`.

---

## Documentation Gaps

### 1. Command Handling (Sections 9-12)

**Status**: Incomplete  
**Priority**: Medium

USER_GUIDE.md Sections 9-12 not yet written:
- Section 9: Command Handling
- Section 10: Configuration and Deployment  
- Section 11: Best Practices
- Section 12: Troubleshooting

**Workaround**: Examples 01-02 demonstrate basic patterns. Command handling shown in copilot-instructions.md.

### 2. Advanced Examples Missing

**Status**: Partially complete  
**Priority**: Low

Examples now created: loop_mode_example.cpp, command_example.cpp, multi_output_runtime.cpp, multi_output_sensor_fusion.cpp.

Still missing:
- Error recovery example
- Performance profiling example

---

## Testing Gaps

### 1. Multi-Input Integration Tests

**Status**: Incomplete  
**Priority**: High

No comprehensive integration tests for multi-input synchronization:
- Test get_data with various tolerance values
- Test rate mismatch scenarios (fast primary + slow secondary)
- Test missing secondary input handling
- Test stale data detection
- Test 3+ input scenarios

### 2. Performance Benchmarks

**Status**: Not Started  
**Priority**: Medium

Need performance characterization:
- Message latency (end-to-end)
- CPU usage at various rates
- Memory footprint
- Max sustainable message rate
- Multi-input synchronization overhead

---

## Future Work

### ROS 2 Adapter

Separate repository planned: `rclcpp-commrat`
- Bridge between ROS 2 and CommRaT
- Topic mapping configuration
- QoS profile handling

### DDS Compatibility Layer

Bridge to standard DDS implementations for interoperability.

### Static Analysis Tools

Real-time safety verification:
- Detect dynamic allocation in hot paths
- Verify bounded execution time
- Check for blocking operations

---

## Reporting New Issues

When reporting issues, please include:

1. **CommRaT Version/Commit**: Git commit hash
2. **Symptom**: What's happening vs what should happen
3. **Reproduction**: Minimal example that demonstrates issue
4. **Logs**: Relevant terminal output with timestamps
5. **Config**: ModuleConfig values used
6. **Environment**: OS, compiler version, TIMS version

**Template**:
```markdown
## Issue Title

**Status**: Active/Resolved  
**Priority**: High/Medium/Low

**Symptom**: ...

**Expected**: ...

**Reproduction**: ...

**Investigation**: ...

**Workaround**: ...
```
