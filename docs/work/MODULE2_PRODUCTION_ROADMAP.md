# Module2 Production Roadmap

## Current Status (Phase 1.5 COMPLETE)

**What Works:**
- ✅ I/O tuple architecture (BuildIOTuple, Module2 template)
- ✅ Zero-copy workspace API (ModuleOutput::get_workspace())
- ✅ Structured I/O access (inputs()/outputs() with named fields)
- ✅ Multi-output support (separate Output<T> specs)
- ✅ Multi-input support (Input<T> + SyncedInput<T>)
- ✅ GetData protocol (consumer RPC + producer handlers)
- ✅ Synced<T> wrapper (fresh/stale/invalid handling)
- ✅ Execution mode auto-inference (Input/Period/Loop)
- ✅ Tests: test_module_output_getdata (all passing)

**What's Missing:**
- ❌ Subscription protocol (Subscribe/Unsubscribe handlers)
- ❌ TypedMailbox/WorkMailbox compilation issues
- ❌ Command infrastructure integration
- ❌ Integration tests (complete Module2 data flow)
- ❌ Performance validation
- ❌ Production examples
- ❌ Migration from Module to Module2 in commrat.hpp

## Blocking Issues

### 0. Registry Design Cleanup (NEW - FOUNDATIONAL)

**Problem**: Multiple overlapping registry concepts causing confusion

**Issues**:
- 5 different registry-related types (MessageRegistry, SystemRegistry, CombinedRegistry, Registry<>, CommRaT<>)
- TypedMailbox template confusion (expects Registry but gets payload types)
- Missing utilities (get_commands_for<T>, filter_by_prefix, etc.)
- message_registry.hpp is 720 lines (should be ~200)
- WorkMailbox type undefined (needed for Module2)

**Impact**: Blocks subscription protocol, confuses TypedMailbox usage

**Solution**: See REGISTRY_CONSOLIDATION.md for complete plan

**Quick Wins** (High Priority):
1. Create registry_utils.hpp with query helpers (2-3 hours)
2. Complete CommRaT::System implementation with WorkMailbox (1-2 hours)
3. Rename TypedMailbox → RestrictedMailbox for clarity (1 hour)

**Priority**: **CRITICAL** - Should be done BEFORE Phase 1.6

**Estimated**: 4-6 hours for essentials

---

### 1. TypedMailbox Template Errors

**Problem**: 
```cpp
// module2.hpp line 213
std::optional<WorkMailbox> work_mailbox_;
// Error: WorkMailbox type not defined
// TypedMailbox template expects MessageRegistry, gets payload types instead
```

**Root Cause**:
- TypedMailbox expects `TypedMailbox<Registry, MessageDefs...>`
- Module2 attempts to use reply payloads instead of MessageDefinitions
- Mailbox<> static_assert fails: "All template parameters must be MessageDefinition types"

**Solution Required**:
1. Define WorkMailbox properly as TypedMailbox with SystemRegistry
2. Fix template instantiation in module2.hpp
3. Test WORK mailbox creation and usage

**Priority**: HIGH - Blocks subscription protocol

**Estimated**: 2-4 hours

---

### 2. Subscription Protocol Implementation

**Problem**: ModuleOutput has no Subscribe/Unsubscribe handlers

**What's Needed**:

```cpp
// ModuleOutput needs:
void handle_subscribe_request(const SubscribeRequestPayload& req);
void handle_unsubscribe_request(const UnsubscribeRequestPayload& req);

// With:
std::vector<SubscriberInfo> continuous_subscribers_;  // Track subscribers

struct SubscriberInfo {
    uint32_t data_mbx_addr;           // Subscriber's DATA mailbox
    int64_t requested_period_ms;      // Requested update period
    uint32_t reduction;               // Send every Nth message
    bool get_next_data;               // One-shot mode
};
```

**Implementation Steps**:
1. Add continuous_subscribers_ member to ModuleOutput
2. Implement handle_subscribe_request():
   - Validate subscriber request
   - Add to continuous_subscribers_ list
   - Send SubscribeReply (success/failure)
3. Implement handle_unsubscribe_request():
   - Find subscriber in list
   - Remove subscriber
   - Send UnsubscribeReply
4. Update publish() to iterate subscribers and send to DATA mailboxes
5. Test subscription flow

**Priority**: HIGH - Core feature

**Estimated**: 4-6 hours

---

### 3. Command Infrastructure

**Problem**: DataWithCommands not integrated into Module2

**What's Needed**:

```cpp
// Module2 needs:
void command_loop() {
    // Receive commands from CMD mailbox
    // Dispatch to user's on_command() handler
    // Send command replies
}

// ModuleOutput needs:
template<typename CmdType>
void handle_command(const TimsMessage<typename CmdType::Payload>& cmd,
                   TimsMessage<typename CmdType::ReplyType>& reply);
```

**Dependencies**:
- CommandMailbox currently commented out in ModuleOutput (line 37)
- Depends on fixing TypedMailbox issues

**Implementation Steps**:
1. Fix TypedMailbox (prerequisite)
2. Uncomment and fix CommandMailbox in ModuleOutput
3. Implement command_loop() in Module2
4. Add command dispatch based on message ID
5. Connect to user's on_command() handler
6. Test command sending and handling

**Priority**: MEDIUM - Important but not blocking

**Estimated**: 6-8 hours

---

## Phase 1.6: Subscription Protocol (NEXT)

### Objectives
1. Fix TypedMailbox/WorkMailbox compilation
2. Implement Subscribe/Unsubscribe handlers
3. Enable continuous data streaming
4. Test complete subscription flow

### Tasks

#### Task 1.6.1: Fix TypedMailbox/WorkMailbox (2-4 hours)

**File**: `include/commrat/module2.hpp`

**Changes**:
```cpp
// Current (BROKEN):
std::optional<WorkMailbox> work_mailbox_;  // WorkMailbox undefined

// Fix 1: Define WorkMailbox type
using SystemMsgRegistry = MessageRegistry<
    SubscribeRequest,
    SubscribeReply,
    UnsubscribeRequest,
    UnsubscribeReply
>;
using WorkMailbox = TypedMailbox<SystemMsgRegistry, 
                                 SubscribeReply, 
                                 UnsubscribeReply>;

// Fix 2: Initialize in constructor
work_mailbox_.emplace(calculate_work_mailbox_address(config.system_id, config.instance_id));
```

**Test**: Compile module2.hpp successfully

---

#### Task 1.6.2: Implement Subscribe Handler (2-3 hours)

**File**: `include/commrat/module/io/output/module_output.hpp`

**Changes**:
```cpp
// Add subscriber tracking
std::vector<SubscriberInfo> continuous_subscribers_;

// Implement handler
void handle_subscribe_request(const SubscribeRequestPayload& req) {
    // Validate request
    if (req.flags & SUBSCRIBE_CONTINUOUS) {
        // Calculate reduction ratio
        uint32_t reduction = calculate_reduction(req.period_ms, period_ms_);
        
        // Add subscriber
        continuous_subscribers_.push_back({
            .data_mbx_addr = req.caller_mbx,
            .requested_period_ms = req.period_ms,
            .reduction = reduction,
            .get_next_data = false
        });
        
        // Send reply
        return SubscribeReplyPayload{.success = true};
    }
    
    return SubscribeReplyPayload{.success = false, .error = "Invalid request"};
}
```

**Test**: Subscribe request → successful reply

---

#### Task 1.6.3: Implement Unsubscribe Handler (1-2 hours)

**File**: `include/commrat/module/io/output/module_output.hpp`

**Changes**:
```cpp
void handle_unsubscribe_request(const UnsubscribeRequestPayload& req) {
    // Find subscriber
    auto it = std::find_if(continuous_subscribers_.begin(), 
                          continuous_subscribers_.end(),
                          [&](const SubscriberInfo& sub) {
                              return sub.data_mbx_addr == req.caller_mbx;
                          });
    
    if (it != continuous_subscribers_.end()) {
        continuous_subscribers_.erase(it);
        return UnsubscribeReplyPayload{.success = true};
    }
    
    return UnsubscribeReplyPayload{.success = false, .error = "Not subscribed"};
}
```

**Test**: Unsubscribe request → successful reply

---

#### Task 1.6.4: Integrate with publish() (1-2 hours)

**File**: `include/commrat/module/io/output/module_output.hpp`

**Changes**:
```cpp
void publish_workspace(Timestamp timestamp) {
    // Fill header and finalize buffer
    auto& msg = output_buffer_.back();
    msg.header = {...};
    output_buffer_.finalize_write(timestamp);
    
    // Send to all continuous subscribers
    global_data_count_++;
    for (auto& sub : continuous_subscribers_) {
        // Check reduction ratio
        if (global_data_count_ % sub.reduction == 0) {
            mailbox_.send(msg, sub.data_mbx_addr);
        }
        
        // Handle one-shot subscriptions
        if (sub.get_next_data) {
            // Remove after first send
            sub.get_next_data = false;
        }
    }
    
    // Clean up one-shot subscribers
    continuous_subscribers_.erase(
        std::remove_if(continuous_subscribers_.begin(),
                      continuous_subscribers_.end(),
                      [](const auto& sub) { return !sub.get_next_data; }),
        continuous_subscribers_.end()
    );
}
```

**Test**: Publish → data sent to subscribers

---

#### Task 1.6.5: Integration Test (2-3 hours)

**File**: `test/test_subscription_protocol.cpp`

**Test Cases**:
1. Subscribe → Receive continuous data stream
2. Subscribe with period reduction (10Hz producer, 5Hz subscriber)
3. Unsubscribe → Stop receiving data
4. Multiple subscribers
5. One-shot subscription (get_next_data)

**Success Criteria**:
- All subscription flows work end-to-end
- Data reduction works correctly
- Unsubscribe stops data flow
- No memory leaks

---

## Phase 1.7: Command Integration (AFTER 1.6)

### Objectives
1. Fix CommandMailbox integration
2. Implement command_loop()
3. Add command dispatch
4. Test command flow

### Tasks

#### Task 1.7.1: Fix CommandMailbox (2-3 hours)

**File**: `include/commrat/module/io/output/module_output.hpp`

**Changes**:
```cpp
// Current (COMMENTED):
// std::optional<CommandMailbox> cmd_mailbox_;

// Fix: Uncomment and define properly
using CommandMsgRegistry = /* Extract commands from DataType */;
using CommandMailbox = TypedMailbox<CommandMsgRegistry, /* command replies */>;

std::optional<CommandMailbox> cmd_mailbox_;

// Initialize in constructor
cmd_mailbox_.emplace(calculate_cmd_mailbox_address(...));
```

---

#### Task 1.7.2: Implement command_loop() (3-4 hours)

**File**: `include/commrat/module2.hpp`

**Changes**:
```cpp
void command_loop() {
    while (!should_stop_) {
        // Receive command from cmd_mailbox
        // Dispatch based on message ID
        // Call user's on_command() handler
        // Send reply
    }
}

// User override point
virtual void on_command(const auto& cmd, auto& reply) {
    // Default: No-op or error
}
```

---

#### Task 1.7.3: Integration Test (2-3 hours)

**File**: `test/test_command_handling.cpp`

**Test Cases**:
1. Send command → Receive reply
2. Command with DataWithCommands association
3. Invalid command → Error reply
4. Command timeout handling

---

## Phase 1.8: Production Readiness (FINAL)

### Objectives
1. Complete integration tests
2. Performance validation
3. Documentation
4. Replace Module with Module2

### Tasks

#### Task 1.8.1: Integration Tests (4-6 hours)

**Tests Needed**:
1. Multi-input module (Input<T> + SyncedInput<T>)
2. Multi-output module (Output<T1>, Output<T2>)
3. Complete data flow (Subscribe → GetData → Continuous streaming)
4. Command handling in multi-module system
5. Period reduction and synchronization
6. Error handling and edge cases

---

#### Task 1.8.2: Performance Validation (2-3 hours)

**Metrics**:
- Zero-copy verification (address comparison tests)
- Latency measurement (timestamp deltas)
- Throughput testing (messages/second)
- CPU usage (should be 0% when idle)
- Memory footprint (no leaks, bounded usage)

**Success Criteria**:
- No unexpected copies (verified via address comparison)
- Latency <1ms for local messages
- Throughput >10kHz for small messages
- Zero CPU when idle (blocking receives work)

---

#### Task 1.8.3: Documentation (3-4 hours)

**Documents to Create/Update**:
1. `docs/MODULE2_USER_GUIDE.md` - User-facing API documentation
2. `docs/MIGRATION_GUIDE.md` - Module → Module2 migration
3. `examples/module2_complete_example.cpp` - Full working example
4. Update README.md with Module2 as primary API
5. API reference documentation (Doxygen)

---

#### Task 1.8.4: Replace Module in commrat.hpp (1 hour)

**File**: `include/commrat/commrat.hpp`

**Changes**:
```cpp
// Uncomment Module2
#include "commrat/module2.hpp"

// Deprecate old Module
// #include "commrat/module.hpp"  // DEPRECATED: Use Module2
```

**Warning**: Breaking change - requires updating all examples

---

#### Task 1.8.5: Update Examples (4-6 hours)

**Examples to Update**:
1. `examples/continuous_input_example.cpp` → Module2
2. `examples/multi_input_example.cpp` → Module2
3. `examples/command_example.cpp` → Module2 with commands
4. Create new: `examples/subscription_example.cpp`
5. Create new: `examples/getdata_example.cpp`

---

## Timeline Estimate

### Phase 0: Registry Consolidation (PREREQUISITE)
- Create registry_utils.hpp: 2-3 hours
- Complete CommRaT::System: 1-2 hours  
- Rename TypedMailbox: 1 hour
- Testing: 1 hour
- **Total**: 5-7 hours (1 day)
- **See**: REGISTRY_CONSOLIDATION.md

### Phase 1.6: Subscription Protocol
- TypedMailbox fixes: 2-4 hours
- Subscribe handler: 2-3 hours
- Unsubscribe handler: 1-2 hours
- Publish integration: 1-2 hours
- Integration test: 2-3 hours
- **Total**: 8-14 hours (1-2 days)

### Phase 1.7: Command Integration
- CommandMailbox fix: 2-3 hours
- command_loop(): 3-4 hours
- Integration test: 2-3 hours
- **Total**: 7-10 hours (1 day)

### Phase 1.8: Production Readiness
- Integration tests: 4-6 hours
- Performance validation: 2-3 hours
- Documentation: 3-4 hours
- Replace Module: 1 hour
- Update examples: 4-6 hours
- **Total**: 14-20 hours (2-3 days)

### **Grand Total**: 34-51 hours (5-7 days of focused work)

**Critical Path**:
1. Registry consolidation (Phase 0) - prerequisite for everything
2. Subscription protocol (Phase 1.6) - core feature
3. Command integration (Phase 1.7) - important
4. Production readiness (Phase 1.8) - polish

---

## Risk Assessment

### High Risk
- **TypedMailbox template issues**: May be deeper than expected
  - Mitigation: Simplify WorkMailbox to basic Mailbox if needed
  
- **Subscription protocol complexity**: Multi-threading, synchronization
  - Mitigation: Start with single-threaded test, add threading later

### Medium Risk
- **Command dispatch performance**: Message ID lookup overhead
  - Mitigation: Use compile-time dispatch (visitor pattern)

- **Memory management**: subscriber list allocation
  - Mitigation: Use fixed_vector instead of std::vector

### Low Risk
- **Documentation**: Clear design, straightforward to document
- **Examples**: Existing patterns can be adapted

---

## Success Criteria

**Module2 is production-ready when:**
- [ ] All compilation errors resolved (TypedMailbox, WorkMailbox)
- [ ] Subscription protocol works (Subscribe, Unsubscribe, continuous streaming)
- [ ] GetData protocol tested and validated
- [ ] Command infrastructure integrated
- [ ] Zero-copy guarantees verified
- [ ] All integration tests pass
- [ ] Performance metrics met
- [ ] Documentation complete
- [ ] Examples updated and working
- [ ] Module2 replaces Module in commrat.hpp
- [ ] No regressions in existing tests

---

## Next Immediate Actions

**PHASE 0 COMES FIRST - Registry Consolidation**

1. **Start Phase 0.1**: Create registry_utils.hpp
   - File: `include/commrat/messaging/registry_utils.hpp`
   - Implement query helpers (get_commands_for, filter_by_prefix, etc.)
   - Test compile-time queries

2. **Complete Phase 0.2**: Enhance CommRaT::System
   - File: `include/commrat/commrat.hpp`
   - Define System::messages(), payload_types()
   - Create System::WorkMailbox type
   - Test system message queries

3. **Then proceed to Phase 1.6**: Subscription protocol

**See REGISTRY_CONSOLIDATION.md for details**

**Ready to proceed with Phase 0.1?**
