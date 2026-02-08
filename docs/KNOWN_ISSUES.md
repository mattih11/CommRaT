# CommRaT Known Issues

This document tracks known issues, limitations, and areas requiring investigation in CommRaT.

**Last Updated**: February 8, 2026  
**Current Phase**: Phase 6.10 Complete (Timestamp Metadata Accessors)

---

## Active Issues

### 1. Multi-Input getData Synchronization (High Priority)

**Status**: 🔴 Not Working  
**Affected**: Example 03 (Multi-Input Fusion), test_multi_input_module  
**Introduced**: Phase 6.9 (Multi-Input Synchronization)

**Symptom**:
- Multi-input modules fail with "Failed to sync inputs" errors
- `getData(timestamp, tolerance)` queries always fail
- Secondary inputs never successfully synchronize with primary input
- Fusion modules receive primary input but cannot retrieve synchronized secondary inputs

**Expected Behavior**:
- Primary input blocks on `receive()` (e.g., IMU at 100Hz)
- Secondary inputs use `getData(primary_timestamp, tolerance)` to fetch synchronized data
- HistoricalMailbox should buffer recent messages and return closest match within tolerance
- Multi-input `process(const T1&, const T2&)` should be called with synchronized inputs

**Investigation Notes**:
- HistoricalMailbox appears to receive and buffer secondary input messages correctly
- `secondary_input_receive_loop[1]` shows messages arriving with timestamps
- getData queries fail despite messages being in the historical buffer
- May be timestamp mismatch or tolerance calculation issue
- Config shows `sync_tolerance=50ms` but queries still fail

**Workaround**:
- None currently available
- Example 03 serves as API documentation only
- Multi-input modules cannot be used in production until resolved

**Files to Check**:
- `include/commrat/mailbox/historical_mailbox.hpp` (getData implementation)
- `include/commrat/mailbox/timestamped_ring_buffer.hpp` (buffer search logic)
- `include/commrat/module/loops/loop_executor.hpp` (multi_input_loop)
- `docs/examples/03_multi_input_fusion/multi_input_fusion.cpp`
- `test/test_3input_fusion.cpp`

**Next Steps**:
1. Add debug logging to getData to see query parameters vs buffer contents
2. Verify timestamp units match (nanoseconds vs milliseconds)
3. Check if historical buffer is actually populated when getData is called
4. Verify sync_tolerance is correctly converted from Milliseconds to nanoseconds
5. Test with simpler 2-input case at same rates (eliminate rate mismatch as factor)

---

## Resolved Issues

### ~~Output<void> Pattern (Phase 5.2)~~ ✅ RESOLVED

**Resolution**: Use pass-through pattern `Output<T>` returning input type instead of `Output<void>`. Example 01 demonstrates correct pattern.

---

## Documentation Gaps

### 1. Command Handling (Sections 9-12)

**Status**: 🟡 Incomplete  
**Priority**: Medium

USER_GUIDE.md Sections 9-12 not yet written:
- Section 9: Command Handling
- Section 10: Configuration and Deployment  
- Section 11: Best Practices
- Section 12: Troubleshooting

**Workaround**: Examples 01-02 demonstrate basic patterns. Command handling shown in copilot-instructions.md.

### 2. Advanced Examples Missing

**Status**: 🟡 Incomplete  
**Priority**: Low

Examples not yet created:
- Loop mode example (maximum throughput)
- Command handling example
- Error recovery example
- Performance profiling example

---

## Design Limitations

### 1. Type-Based Metadata Access Limited to 2 Types

**Status**: 🟡 By Design (Current Implementation)  
**Priority**: Low

`get_input_metadata<T>()` type-based access only works for the first 2 input types due to tuple unpacking implementation limits.

**Workaround**: Use index-based access `get_input_metadata<0>()` for 3+ inputs.

**Files**: `include/commrat/module/metadata/input_metadata_accessors.hpp`

### 2. Command Dispatch Needs Improvement

**Status**: 🟡 Known Limitation  
**Priority**: Medium

Current variadic command handling with overloaded `on_command()` functions works but is not elegant. Better dispatch mechanism needed.

**Workaround**: Use `on_command(const CmdType&) override` for each command type.

---

## Testing Gaps

### 1. Multi-Input Integration Tests

**Status**: 🔴 Incomplete  
**Priority**: High

No comprehensive integration tests for multi-input synchronization:
- Test getData with various tolerance values
- Test rate mismatch scenarios (fast primary + slow secondary)
- Test missing secondary input handling
- Test stale data detection
- Test 3+ input scenarios

### 2. Performance Benchmarks

**Status**: 🟡 Not Started  
**Priority**: Medium

Need performance characterization:
- Message latency (end-to-end)
- CPU usage at various rates
- Memory footprint
- Max sustainable message rate
- Multi-input synchronization overhead

---

## Future Work (Post-Phase 7)

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
2. **Phase**: Current development phase
3. **Symptom**: What's happening vs what should happen
4. **Reproduction**: Minimal example that demonstrates issue
5. **Logs**: Relevant terminal output with timestamps
6. **Config**: ModuleConfig values used
7. **Environment**: OS, compiler version, TIMS version

**Template**:
```markdown
## Issue Title

**Status**: 🔴/🟡/🟢  
**Priority**: High/Medium/Low  
**Introduced**: Phase X.X

**Symptom**: ...

**Expected**: ...

**Reproduction**: ...

**Investigation**: ...

**Workaround**: ...
```

---

## Issue Status Legend

- 🔴 **Not Working**: Feature broken, blocks functionality
- 🟡 **Limitation**: Works with constraints, workaround available  
- 🟢 **Resolved**: Issue fixed, documented for reference
- ⚠️ **Under Investigation**: Active debugging in progress
