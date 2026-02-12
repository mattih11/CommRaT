# CommRaT Known Issues

This document tracks known issues, limitations, and areas requiring investigation in CommRaT.

**Last Updated**: February 11, 2026

---

## Active Issues

### 1. Type-Based Metadata Access Limited to 2 Types

**Status**: By Design (Current Implementation)  
**Priority**: Low

`get_input_metadata<T>()` type-based access only works for the first 2 input types due to tuple unpacking implementation limits.

**Workaround**: Use index-based access `get_input_metadata<0>()` for 3+ inputs.

**Files**: `include/commrat/module/metadata/input_metadata_accessors.hpp`

### 2. Command Dispatch Needs Improvement

**Status**: Known Limitation  
**Priority**: Medium

Current variadic command handling with overloaded `on_command()` functions works but is not elegant. Better dispatch mechanism needed.

**Workaround**: Use `on_command(const CmdType&) override` for each command type.

### 3. No Command Reply Mechanism

**Status**: Feature Gap  
**Priority**: Medium

Commands are currently one-way (fire-and-forget). No built-in support for request/reply patterns like GetStatus or SetParameter commands.

**Proposed Solutions** (see `docs/internal/phase_history/ARCHITECTURE_ANALYSIS.md` Phase 6):
- **Pattern 1**: One-way commands (current, works as-is)
- **Pattern 2**: Reply via output parameter: `void on_command(const GetStatusCmd&, StatusReply&)`
- **Pattern 3**: Reply via return value: `StatusReply on_command(const GetStatusCmd&)`

**Implementation Needs**:
- Reply routing infrastructure in command handler
- Blocking wait for reply on sender side
- Timeout mechanism (recommended: 1-second default)
- Concept detection: `has_reply<CmdT, ReplyT>`, `returns_reply<CmdT>`

**Workaround**: Use regular output messages for replies (manual correlation via sequence numbers).

---

## Resolved Issues

### 1. Multi-Input getData Synchronization (RESOLVED - High Priority)

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
- **Result**: All getData queries failed with "no matching message within tolerance"

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
- Example 03 now shows successful fusion: `[Fusion] #100 | GPS: ✓fresh age=17.7405ms`
- Sync ages well within 100ms tolerance: 17ms, 36ms, 66ms, 87ms, 7ms, 27ms
- Monitor receives continuous stream of fused outputs
- Phase 6.9 Multi-Input Synchronization feature **now fully functional**

**Lessons Learned**:
- Unit mismatches (ms vs ns) are subtle but catastrophic bugs
- Comments like "unit depends on usage" are dangerous - should be explicit
- Parameter naming matters: `tolerance_units` was ambiguous, `tolerance_ns` is clear
- Testing with realistic data rates is crucial to catch magnitude errors

---

## Resolved Issues

### 2. Output<void> Pattern (RESOLVED)

**Resolution**: Use pass-through pattern `Output<T>` returning input type instead of `Output<void>`. Example 01 demonstrates correct pattern.

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

**Status**: Incomplete  
**Priority**: Low

Examples not yet created:
- Loop mode example (maximum throughput)
- Command handling example
- Error recovery example
- Performance profiling example

---

## Design Limitations

### 1. Type-Based Metadata Access Limited to 2 Types

**Status**: By Design (Current Implementation)  
**Priority**: Low

`get_input_metadata<T>()` type-based access only works for the first 2 input types due to tuple unpacking implementation limits.

**Workaround**: Use index-based access `get_input_metadata<0>()` for 3+ inputs.

**Files**: `include/commrat/module/metadata/input_metadata_accessors.hpp`

### 2. Command Dispatch Needs Improvement

**Status**: Known Limitation  
**Priority**: Medium

Current variadic command handling with overloaded `on_command()` functions works but is not elegant. Better dispatch mechanism needed.

**Workaround**: Use `on_command(const CmdType&) override` for each command type.

---

## Testing Gaps

### 1. Multi-Input Integration Tests

**Status**: Incomplete  
**Priority**: High

No comprehensive integration tests for multi-input synchronization:
- Test getData with various tolerance values
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
