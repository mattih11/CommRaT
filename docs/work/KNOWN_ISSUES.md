# Known Issues in CommRaT

This document tracks active issues, their symptoms, workarounds, and resolution status.

## RESOLVED: System Messages Not Included in Main Registry

**Status:** ✓ RESOLVED (2026-02-22, Phase 0.2)  
**Was Blocking:** Phase 1.6 (Subscription Protocol)  
**Priority:** Was P0 - Critical blocker

**Symptoms:**
- `CommRaT<MessageDefs...>` only includes user-defined messages
- System messages (Subscribe, Unsubscribe, GetData, etc.) in separate `SystemRegistry` struct
- `subscription_messages_t<MyApp>` returns empty tuple  
- `WorkMailbox` undefined (no way to create mailbox for system messages)
- Module2 subscription handlers blocked

**Root Cause:**
- `commrat.hpp` line 119: Creates separate `SysReg = MessageRegistry<SubscribeRequest, Unsubscribe Request>`
- These are never merged into main `MessageRegistry<MessageDefs...>`  
- Documentation claims "Automatic system message inclusion" but doesn't actually do it

**Expected Behavior:**
- All system messages should be part of main registry
- `MyApp::get_message_id<SubscribeRequestPayload>()` should work
- `subscription_messages_t<MyApp>` should return Subscribe/Unsubscribe messages
- `WorkMailbox` should be definable as `RestrictedMailbox<subscription_messages_t<MyApp>>`

**Workaround:**
- None currently - this blocks WorkMailbox definition and subscription protocol

**Fix Required:**
- **Option 1:** Merge system messages into MessageRegistry expansion (preferred)
  - Modify MessageRegistry to auto-include system messages in ProcessedDefs
  - Update ExpandReplies or create ExpandSystemMessages step
  
- **Option 2:** Make CommRaT inherit from combined registry
  - `CommRaT : public MessageRegistry<MessageDefs..., SubscribeRequest, UnsubscribeRequest>`
  - Simpler but less flexible

**Timeline:** ✓ FIXED in Phase 0.2 (1.5 hours)

---

## RESOLVED: Reply Message Detection Broken

**Status:** ✓ RESOLVED (2026-02-22, Phase 0.1)  
**Was Breaking:** Filtered queries  
**Priority:** Was P1 - Critical

**Symptoms:**
- `filter_replies_t<MyApp>` returns 0 messages even when replies exist
- `reply_message_count<MyApp>()` returns 0
- Request/reply symmetry check fails
- GetData reply messages not detected

**Root Cause:**
- `registry_utils.hpp` line ~339: `IsReply` uses wrong logic  
- Current: `MessageDef::has_reply && !MessageDef::is_request`
- Problem: Reply messages have `has_reply=false` (they don't "have" a reply, they ARE a reply)
- Correct detection: Check if `local_id > MAX_MESSAGE_ID` (sign bit set)

**Expected Behavior:**
- Reply messages should be detected by their inverted local_id
- `filter_replies_t<>` should return all reply messages
- Request/reply counts should match

**Fix Applied:**
```cpp
template<typename MessageDef>
struct IsReply {
    // Reply messages have local_id > MAX_MESSAGE_ID (sign bit set)
    static constexpr bool value = MessageDef::local_id > MAX_MESSAGE_ID;
};
```

**Timeline:** ✓ FIXED in Phase 0.1 (immediate)

---

## Issue: GetData Message Expansion Creates Noise

**Status:** DESIGN - Works as intended but clutters registry  
**Discovered:** 2026-02-22 during registry_utils.hpp testing  
**Priority:** P2 - UX improvement, not blocking

**Symptoms:**
- Each `Message::Data<T>` expands to 5 messages (Data + 2 GetData + 2 GetNextData)
- Registry size() much larger than user expects
- Filters must account for GetData subprefixes (3, 4) vs Data subprefix (0)

**Current Behavior:**
```
Message::Data<TempData> expands to:
  - TempData (prefix=1, subprefix=0) 
  - GetDataRequest<TempData> (prefix=1, subprefix=3)
  - GetDataReply<TempData> (prefix=1, subprefix=3)
  - GetNextDataRequest<TempData> (prefix=1, subprefix=4)
  - GetNextDataReply<TempData> (prefix=1, subprefix=4)
```

**Expected Behavior:** (No change - this is intentional)

**Workaround:**
- Use `data_messages_t<>` to filter only subprefix=0 (Data) messages
- Use `filter_by_subprefix<UserDefined, GetData>` for GetData messages specifically

**Fix:** None needed - document clearly in user-facing docs

---

## Resolution Log

### Resolved Issues

#### Phase 1.8 - Subscription Protocol Implementation (COMPLETE)
**Resolved:** 2026-02-23 (Phase 1.8)  
**Feature:** Full subscription protocol with data delivery

**Implemented:**
1. **Subscribe/Unsubscribe Handlers** in ModuleOutput:
   - `handle_subscribe_request()` - Adds subscribers with reduction calculation
   - `handle_unsubscribe_request()` - Removes subscribers from list
   - Calculates reduction ratio based on producer/consumer period mismatch
   - Supports one-shot mode (get_next_data) for single message delivery

2. **GetData Protocol** in ModuleOutput:
   - `handle_get_data_request()` - Timestamp-synchronized query
   - `handle_get_next_data_request()` - Latest data fetch
   - Searches ring buffer for matching timestamps
   - Returns data with freshness metadata for SyncedInput

3. **Command Dispatch** in CommandHandler:
   - Type traits to detect GetDataRequestPayload<T> and GetNextDataRequestPayload<T>
   - System command visitor handles Subscribe/Unsubscribe/GetData/GetNextData
   - All replies sent via `send_reply()` (RACK-style, dest auto-extracted)

4. **Dual Mailbox Architecture per Output**:
   - **CMD mailbox** (receive-only): Handles SubscribeRequest, UnsubscribeRequest, GetDataRequest
   - **PUBLISH mailbox** (send-only): Sends output data type T to subscribers
   - Both use same type_id but different base addresses (CMD_MBX_BASE vs WORK_MBX_BASE)

5. **Data Publishing** with Subscriber Delivery:
   - `publish()` sends to all continuous subscribers via PUBLISH mailbox
   - One-shot subscribers removed after delivery
   - Reduction logic: sends every Nth message based on period ratio
   - Data copied to buffer first (for get_data queries), then sent to subscribers

**Architecture:**
```
Per-Output Mailboxes:
├─ CMD mailbox (base+0):  Receives commands (Subscribe, GetData, user commands)
└─ PUBLISH mailbox (base+16): Sends output data to subscribers

Producer::publish(data, timestamp)
  ├─> Buffer data (for get_data queries)
  ├─> For each subscriber:
  │     if (count % reduction == 0):
  │       publish_mailbox.send(data, subscriber.data_mbx_addr, timestamp)
  │       if one-shot: mark for removal
  └─> Remove one-shot subscribers

Consumer sends SubscribeRequest
  ├─> Producer receives on CMD mailbox
  ├─> Producer adds to subscriber list with reduction
  └─> Producer sends SubscribeReply to consumer's WORK mailbox
  
Consumer needs synced data
  ├─> Consumer sends GetDataRequest(timestamp) to Producer's CMD mailbox
  ├─> Producer searches ring buffer
  └─> Producer sends GetDataReply to consumer's CMD mailbox
```

**Files Modified:**
- [module_output.hpp](../../include/commrat/module/io/output/module_output.hpp):
  - Added PUBLISH mailbox (TypedMailbox<CommratApp, T>) for sending output data
  - Fixed `publish()` to use PUBLISH mailbox instead of CMD mailbox
  - Already had subscribe/unsubscribe/get_data handlers (Phase 1.5)
  
- [command_handler.hpp](../../include/commrat/module/services/command_handler.hpp):
  - Added type traits for GetData message detection
  - Added GetData/GetNextData to system command dispatch
  - All system commands handled with send_reply()
  
**Result:**
- Full subscription protocol operational ✓
- Dual mailbox per output (CMD + PUBLISH) ✓
- Reduction logic working (period-based message skipping) ✓
- One-shot mode (get_next_data) implemented ✓
- GetData timestamp synchronization complete ✓
- Builds cleanly ✓

**Next Phase (1.9):** End-to-end testing with actual producer-consumer pair

#### Mailbox Architecture Cleanup (Phase 1.7)
**Resolved:** 2026-02-23 (Phase 1.7)  
**Original Issue:** Unnecessary complexity and duplication in mailbox layer
**Symptoms:**
- `receive_any_raw()` barely used (only in RegistryMailbox polyfill)
- `RawReceivedMessage` struct (120 lines) for one use case
- TypedMailbox has 3 specializations with duplicated send/receive implementations
- RegistryMailbox wrapper only used in few places (redundant layer)
- SendOnlyTypes/ReceiveTypes tag types add complexity without clear benefit

**Fix Applied:**
1. **Removed** `receive_any_raw()` and `RawReceivedMessage` from Mailbox (~150 lines)
2. **Deleted** `registry_mailbox.hpp` entirely (~230 lines)
3. **Deleted** TypedMailbox SendOnlyTypes and ReceiveTypes specializations (~280 lines)
4. **Simplified** TypedMailbox to single base template (type validation only)
5. **Updated** all uses of RegistryMailbox to use bare Mailbox directly
6. **Removed** SendOnlyTypes/ReceiveTypes tag types
7. **Updated** MailboxSet, module_types.hpp, subscription.hpp to use direct Mailbox

**Result:**
- **~660 lines removed** across mailbox layer
- **3 files deleted:** registry_mailbox.hpp removed, specializations gone
- **Clean architecture:** Mailbox (core) → TypedMailbox (validation) → Done
- **No duplication:** TypedMailbox delegates to Mailbox, doesn't reimplement
- **Better buffer control:** Use template parameters for explicit sizing
- **Core library builds cleanly** ✓

**Files Modified:**
- [mailbox.hpp](../../include/commrat/mailbox/mailbox.hpp) - Removed raw handling (~150 lines)
- [typed_mailbox.hpp](../../include/commrat/mailbox/typed_mailbox.hpp) - Removed specializations (~280 lines), now 460 lines
- **DELETED:** registry_mailbox.hpp (~230 lines)
- [system_registry.hpp](../../include/commrat/messaging/system/system_registry.hpp) - Extract Mailbox from Registry
- [subscription.hpp](../../include/commrat/module/services/subscription.hpp) - Use bare Mailbox
- [mailbox_set.hpp](../../include/commrat/module/mailbox/mailbox_set.hpp) - Simplified CMD mailbox types
- [module_types.hpp](../../include/commrat/module/traits/module_types.hpp) - Extract mailbox helper
- [command_example.cpp](../../examples/command_example.cpp) - Use bare Mailbox

**Before/After:**
```
BEFORE:
- mailbox.hpp: ~780 lines (with raw handling)
- typed_mailbox.hpp: ~740 lines (with 3 specializations)
- registry_mailbox.hpp: ~230 lines
- Total: ~1750 lines

AFTER:
- mailbox.hpp: 627 lines (cleaned)
- typed_mailbox.hpp: 460 lines (base only)
- registry_mailbox.hpp: DELETED
- Total: 1087 lines

SAVINGS: 663 lines (38% reduction)
```

#### System Messages Now Included in Main Registry
**Resolved:** 2026-02-22 (Phase 0.2)  
**Fix:** Modified `CommRaT<>` to inherit from `MessageRegistry<MessageDefs..., SubscribeRequest, UnsubscribeRequest>`  
**Result:**
- System messages now part of main registry (not separate)
- `subscription_messages_t<MyApp>` returns 4 messages ✓
- `MyApp::get_message_id<SubscribeRequestPayload>()` works ✓
- WorkMailbox type defined (TypedMailbox compatibility pending)
- Registry size increased by 4 (2 requests + 2 replies)

**Files Modified:**
- [commrat.hpp](../../include/commrat/commrat.hpp#L117-L165) - CommRaT inheritance and System struct

#### Reply Message Detection Fixed
**Resolved:** 2026-02-22 (Phase 0.1)  
**Fix:** Changed `IsReply` to check `local_id > MAX_MESSAGE_ID` instead of `has_reply && !is_request`  
**Result:**
- `filter_replies_t<>` now correctly returns all reply messages
- Request/reply symmetry check passes ✓
- `reply_message_count<>()` returns correct count

**Files Modified:**
- [registry_utils.hpp](../../include/commrat/messaging/registry_utils.hpp#L330-L350) - IsReply implementation

---

## Triage Guidelines

**P0 - BLOCKING:** Prevents next phase work, must fix immediately  
**P1 - CRITICAL:** Breaks existing functionality, fix within current phase  
**P2 - IMPORTANT:** UX/design issue, fix when convenient  
**P3 - NICE TO HAVE:** Future improvement, backlog

