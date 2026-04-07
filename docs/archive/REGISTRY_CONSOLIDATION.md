# Registry Design Consolidation

## Status

**Last Updated:** 2026-02-22  
**Current Phase:** Phase 0 COMPLETE ✅

### Completed
- ✅ **Phase 0.1**: registry_utils.hpp created (2 hours)
  - All query helpers implemented and tested
  - Reply detection bug fixed
  - Tests passing
- ✅ **Phase 0.2**: System message integration (1.5 hours)
  - CommRaT now includes system messages in main registry
  - WorkMailbox type defined
  - All tests passing
- ✅ **Phase 1.6 Infrastructure**: Work loop threading (30 min)
  - work_threads_ array added to Module2
  - work_loop() skeleton implemented
  - Thread lifecycle management (start/stop)

### In Progress
- Phase 1.6: Subscription protocol implementation (ready to continue)

### Remaining
- Phase 0.3: TypedMailbox → RestrictedMailbox rename (1 hour) - OPTIONAL, DEFERRED
- Phase 0.4: Remove deprecated types (30 min) - OPTIONAL, DEFERRED
- Phase 0.5: Split message_registry.hpp (2-3 hours) - DEFERRED

**Total Phase 0 Time:** 4 hours (well under 5-7 hour estimate!)  
**Blocker Removed:** ✓ System messages accessible, WorkMailbox defined, threading ready

---

## Current Problems

### 1. Multiple Overlapping Registry Concepts

**MessageRegistry** (Core):
```cpp
template<typename... MessageDefs>
class MessageRegistry {
    // Auto-ID assignment
    // Reply expansion
    // GetData expansion
    // Collision detection
    // Serialization helpers
};
```

**SystemRegistry** (Alias):
```cpp
using SystemRegistry = MessageRegistry<
    SubscribeRequest,
    UnsubscribeRequest
>;
```

**CombinedRegistry** (Deprecated):
```cpp
template<typename... UserMessageDefs>
using CombinedRegistry = MessageRegistry<
    SubscribeRequest,
    UnsubscribeRequest,
    UserMessageDefs...
>;
```

**Registry** (Wrapper - OLD, UNUSED):
```cpp
template<typename... UserMessageDefs>
class Registry {
    using Type = MessageRegistry<
        SubscribeRequest,
        UnsubscribeRequest,
        UserMessageDefs...
    >;
    
    template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
    using Module = commrat::Module<Type, OutputDataT, InputModeT, CommandTypes...>;
    
    using Mailbox = commrat::RegistryMailbox<Type>;
};
```

**CommRaT** (Current Preferred):
```cpp
template<typename... MessageDefs>
class CommRaT : public MessageRegistry<MessageDefs...> {
    using UserRegistry = MessageRegistry<MessageDefs...>;
    struct SystemRegistry {
        using PayloadTypes = ...;  // Only half-implemented!
        using SystemCommands = ...; 
    };
};
```

**Problem**: 5 different registry-related types doing overlapping things!

### 2. TypedMailbox Confusion

**Current**:
```cpp
// TypedMailbox expects MessageRegistry<MessageDefs...>
template<typename Registry, typename... AllowedPayloadTypes>
class TypedMailbox;

// But module2.hpp tries to use payload types:
using WorkMailbox = TypedMailbox<???, SubscribeReply, UnsubscribeReply>;
// Error: What's the Registry? What are the allowed types?
```

**Problem**: TypedMailbox design doesn't align with registry patterns.

### 3. Missing Utility Accessors

**What we need but don't have**:
```cpp
// Get all commands for a data type
Registry::get_commands_for<SensorData>();  // Returns tuple of command MessageDefs

// Get all system messages
Registry::get_system_messages();  // Returns tuple of system MessageDefs

// Filter by prefix/subprefix
Registry::filter_by_prefix<MessagePrefix::UserDefined>();

// Combine registries
using Combined = Registry::merge<Registry1, Registry2>;

// Get data messages only
Registry::get_data_messages();

// Get command messages only
Registry::get_command_messages();
```

### 4. Inflated Message Registry File

**Current**: `message_registry.hpp` is 720 lines with:
- Auto-ID assignment (complex)
- Reply expansion
- GetData expansion
- Collision detection
- Serialization helpers
- Contains expansion logic that should be utilities

---

## Proposed Consolidation

### Phase 1: Unify Registry Concepts

**Single Registry Design**:
```cpp
// Core message storage (stays as-is, mostly)
template<typename... MessageDefs>
class MessageRegistry {
    // Keep: Auto-ID, collision detection, basic queries
    // Remove: Expansion logic (move to utilities)
};

// Application definition (only one pattern)
template<typename... MessageDefs>
class CommRaT : public MessageRegistry<MessageDefs...> {
public:
    using Registry = MessageRegistry<MessageDefs...>;
    
    // Smart system registry (complete implementation)
    struct System {
        static constexpr auto messages() {
            return std::tuple<
                SubscribeRequest, SubscribeReply,
                UnsubscribeRequest, UnsubscribeReply
                // GetData/GetNextData are type-specific, added dynamically
            >{};
        }
        
        template<typename T>
        static constexpr uint32_t get_message_id();
        
        static constexpr auto payload_types() {
            return std::tuple<
                SubscribeRequestPayload, SubscribeReplyPayload,
                UnsubscribeRequestPayload, UnsubscribeReplyPayload
            >{};
        }
        
        // Typed mailbox for system messages
        using Mailbox = TypedMailbox<System::messages()>;
    };
    
    // User message accessors
    static constexpr auto user_messages() {
        return std::tuple<MessageDefs...>{};
    }
    
    // Combined view (user + system)
    static constexpr auto all_messages() {
        return tuple_cat(user_messages(), System::messages());
    }
    
    // Utilities (see below)
    template<typename DataType>
    static constexpr auto get_commands_for();
    
    template<MessagePrefix P>
    static constexpr auto filter_by_prefix();
    
    static constexpr auto data_messages();
    static constexpr auto command_messages();
};
```

**Remove**:
- ❌ `Registry<>` class (unused, superseded by CommRaT)
- ❌ `CombinedRegistry` (deprecated)
- ❌ Free-standing `SystemRegistry` alias (fold into CommRaT::System)

---

### Phase 2: Registry Utilities (New File)

**Create**: `include/commrat/messaging/registry_utils.hpp`

```cpp
namespace commrat::registry {

// ============================================================================
// Message Filtering and Querying
// ============================================================================

/**
 * @brief Get all command MessageDefs associated with a data type
 * 
 * Extracts commands from DataWithCommands<PayloadType, CmdTypes...>
 * 
 * @tparam DataType The data message payload type
 * @tparam Registry Message registry to search
 * @return Tuple of command MessageDefinitions
 */
template<typename DataType, typename Registry>
constexpr auto get_commands_for() {
    // Implementation: Find DataType in registry, extract commands if DataWithCommands
}

/**
 * @brief Filter messages by prefix
 */
template<MessagePrefix P, typename Registry>
constexpr auto filter_by_prefix() {
    // Returns tuple of MessageDefs with matching prefix
}

/**
 * @brief Filter messages by subprefix
 */
template<MessagePrefix P, uint8_t SubP, typename Registry>
constexpr auto filter_by_subprefix() {
    // Returns tuple of MessageDefs with matching prefix + subprefix
}

/**
 * @brief Get all data messages (UserDefined::Data)
 */
template<typename Registry>
constexpr auto data_messages() {
    return filter_by_subprefix<MessagePrefix::UserDefined, 
                               static_cast<uint8_t>(UserSubPrefix::Data),
                               Registry>();
}

/**
 * @brief Get all command messages (UserDefined::Commands)
 */
template<typename Registry>
constexpr auto command_messages() {
    return filter_by_subprefix<MessagePrefix::UserDefined,
                               static_cast<uint8_t>(UserSubPrefix::Commands),
                               Registry>();
}

/**
 * @brief Get all system control messages
 */
template<typename Registry>
constexpr auto system_control_messages() {
    return filter_by_subprefix<MessagePrefix::System,
                               static_cast<uint8_t>(SystemSubPrefix::Control),
                               Registry>();
}

/**
 * @brief Get all subscription protocol messages
 */
template<typename Registry>
constexpr auto subscription_messages() {
    return filter_by_subprefix<MessagePrefix::System,
                               static_cast<uint8_t>(SystemSubPrefix::Subscription),
                               Registry>();
}

// ============================================================================
// Registry Combination
// ============================================================================

/**
 * @brief Merge multiple registries into one
 */
template<typename... Registries>
struct MergeRegistries {
    // Extract all MessageDefs and combine
    using type = MessageRegistry</* all defs from all registries */>;
};

template<typename... Registries>
using merge_registries_t = typename MergeRegistries<Registries...>::type;

// ============================================================================
// Payload Type Queries
// ============================================================================

/**
 * @brief Extract payload types from MessageDefs tuple
 */
template<typename MessageDefsTuple>
struct ExtractPayloads {
    // Returns std::tuple<PayloadType1, PayloadType2, ...>
};

/**
 * @brief Check if payload type is request message
 */
template<typename PayloadType, typename Registry>
constexpr bool is_request_payload() {
    // Check if PayloadType corresponds to a request message (has_reply)
}

/**
 * @brief Check if payload type is reply message
 */
template<typename PayloadType, typename Registry>
constexpr bool is_reply_payload() {
    // Check if PayloadType is a reply to some request
}

/**
 * @brief Get reply type for request payload
 */
template<typename RequestPayload, typename Registry>
using reply_type_for = /* extract reply payload type */;

} // namespace commrat::registry
```

---

### Phase 3: Simplify TypedMailbox

**Problem**: TypedMailbox template is confusing

**Current**:
```cpp
template<typename Registry, typename... AllowedPayloadTypes>
class TypedMailbox;  // Mixed registry + payload types
```

**Solution 1: Payload-Only Mailbox** (Simpler)
```cpp
template<typename Registry, typename... AllowedPayloadTypes>
class RestrictedMailbox {
    // Uses Registry for all message types
    // But only allows receive/send of AllowedPayloadTypes
    // Compile-time validation: AllowedPayloadTypes must be in Registry
};
```

**Solution 2: MessageDef-Based** (More explicit)
```cpp
template<typename... AllowedMessageDefs>
class FilteredMailbox {
    // Only these specific MessageDefs allowed
    // No registry needed - self-contained
    Mailbox<AllowedMessageDefs...> underlying_;
};
```

**Recommendation**: Solution 1 with rename:
```cpp
// OLD name (confusing)
TypedMailbox<Registry, AllowedTypes...>

// NEW name (clear)
RestrictedMailbox<Registry, AllowedPayloadTypes...>

// Usage in Module2:
using WorkMailbox = RestrictedMailbox<
    CommRaT,  // Full registry
    SubscribeReplyPayload,
    UnsubscribeReplyPayload
>;
```

---

### Phase 4: Slim Down MessageRegistry

**Move OUT of message_registry.hpp**:
1. Auto-ID assignment → `message_id_assignment.hpp`
2. Reply expansion → `reply_expansion.hpp`
3. GetData expansion → `getdata_expansion.hpp`
4. Collision detection → `id_collision_check.hpp`

**Keep IN message_registry.hpp**:
1. Core MessageRegistry class
2. Basic queries (is_registered, get_message_id)
3. Serialization integration
4. PayloadTypes tuple

**Result**: `message_registry.hpp` drops from 720 lines → ~200 lines

---

## Implementation Plan

### Step 1: Create registry_utils.hpp (2-3 hours)

**New file**: `include/commrat/messaging/registry_utils.hpp`

**Implement**:
- [ ] `get_commands_for<T>()`
- [ ] `filter_by_prefix<P>()`
- [ ] `filter_by_subprefix<P, S>()`
- [ ] `data_messages()`
- [ ] `command_messages()`
- [ ] `subscription_messages()`
- [ ] Payload extraction helpers

**Test**: Compile-time tests for each utility

---

### Step 2: Enhance CommRaT::System (1-2 hours)

**File**: `include/commrat/commrat.hpp`

**Add to CommRaT::System**:
```cpp
struct System {
    // Message definitions
    using SubscribeReq = SubscribeRequest;
    using SubscribeRep = SubscribeReply;
    using UnsubscribeReq = UnsubscribeRequest;
    using UnsubscribeRep = UnsubscribeReply;
    
    // All system message defs
    static constexpr auto messages() {
        return std::tuple<
            SubscribeReq, SubscribeRep,
            UnsubscribeReq, UnsubscribeRep
        >{};
    }
    
    // Payload types
    using SubscribeRequestPayload = typename SubscribeReq::Payload;
    using SubscribeReplyPayload = typename SubscribeRep::Payload;
    using UnsubscribeRequestPayload = typename UnsubscribeReq::Payload;
    using UnsubscribeReplyPayload = typename UnsubscribeRep::Payload;
    
    static constexpr auto payload_types() {
        return std::tuple<
            SubscribeRequestPayload, SubscribeReplyPayload,
            UnsubscribeRequestPayload, UnsubscribeReplyPayload
        >{};
    }
    
    // Message ID helpers
    template<typename SystemPayloadType>
    static constexpr uint32_t get_message_id() {
        // Implemented via MessageRegistry lookup
    }
    
    // Mailbox type for system messages
    using WorkMailbox = RestrictedMailbox<
        MessageRegistry<SubscribeReq, SubscribeRep, UnsubscribeReq, UnsubscribeRep>,
        SubscribeReplyPayload,
        UnsubscribeReplyPayload
    >;
};
```

---

### Step 3: Rename TypedMailbox → RestrictedMailbox (1 hour)

**File**: `include/commrat/mailbox/typed_mailbox.hpp`

**Changes**:
```cpp
// Add alias for backward compatibility
template<typename Registry, typename... AllowedPayloadTypes>
using TypedMailbox = RestrictedMailbox<Registry, AllowedPayloadTypes...>;

// Rename class
template<typename Registry, typename... AllowedPayloadTypes>
class RestrictedMailbox {
    // Implementation unchanged
};
```

**Update all usages**:
- module2.hpp: WorkMailbox definition
- module_output.hpp: CommandMailbox definition

---

### Step 4: Remove Deprecated Registry Types (30 min)

**Files to modify**:

1. **system_registry.hpp**:
   - ❌ Remove `Registry<>` class (lines 39-70)
   - ❌ Remove `CombinedRegistry` alias (lines 80-95)
   - ✅ Keep `SystemRegistry` alias (used in comments/docs)

2. **Update documentation**:
   - Remove references to `Registry<>`
   - Remove references to `CombinedRegistry`
   - Document `CommRaT<>` as sole application definition pattern

---

### Step 5: Split message_registry.hpp (2-3 hours)

**Create new files**:

1. `include/commrat/messaging/internal/id_assignment.hpp`
   - Move: `AutoAssignIDs` and related
   
2. `include/commrat/messaging/internal/reply_expansion.hpp`
   - Move: `ExpandReplies`
   
3. `include/commrat/messaging/internal/getdata_expansion.hpp`
   - Move: `ExpandGetDataMessages`, `has_get_data_support`
   
4. `include/commrat/messaging/internal/collision_check.hpp`
   - Move: `CheckCollisions`

**Update message_registry.hpp**:
```cpp
#pragma once

#include "internal/id_assignment.hpp"
#include "internal/reply_expansion.hpp"
#include "internal/getdata_expansion.hpp"
#include "internal/collision_check.hpp"

namespace commrat {

// Slim MessageRegistry class (using internal utilities)
template<typename... MessageDefs>
class MessageRegistry {
    // Core implementation only (~200 lines)
};

} // namespace commrat
```

---

## Benefits of Consolidation

### 1. Clearer Mental Model
- **ONE application pattern**: `CommRaT<MessageDefs...>`
- **ONE mailbox restriction pattern**: `RestrictedMailbox<Registry, Allowed...>`
- **ONE utility namespace**: `registry::get_commands_for()`, etc.

### 2. Better Discoverability
```cpp
// Users can now:
MyApp::System::messages();           // Get system messages
MyApp::user_messages();              // Get user messages
MyApp::all_messages();               // Get combined view

registry::get_commands_for<SensorData>();  // Get associated commands
registry::data_messages<MyApp>();          // Get all data messages
```

### 3. Smaller, More Focused Files
- `message_registry.hpp`: 720 → 200 lines
- Expansion logic isolated in `internal/` folder
- Clear separation of concerns

### 4. Easier to Extend
```cpp
// Adding new registry queries is trivial:
template<typename Registry>
constexpr auto event_messages() {
    return filter_by_subprefix<MessagePrefix::UserDefined,
                               static_cast<uint8_t>(UserSubPrefix::Events),
                               Registry>();
}
```

### 5. Fix TypedMailbox Confusion
```cpp
// BEFORE (confusing)
using WorkMailbox = TypedMailbox<???, SubscribeReply, UnsubscribeReply>;

// AFTER (clear)
using WorkMailbox = RestrictedMailbox<MyApp, SubscribeReplyPayload, UnsubscribeReplyPayload>;
// OR: Use CommRaT::System::WorkMailbox (pre-defined!)
```

---

## Timeline

**Total Estimated**: 8-12 hours

| Phase | Task | Time | Priority |
|-------|------|------|----------|
| 1 | Create registry_utils.hpp | 2-3 hours | HIGH |
| 2 | Enhance CommRaT::System | 1-2 hours | HIGH |
| 3 | Rename TypedMailbox | 1 hour | MEDIUM |
| 4 | Remove deprecated types | 30 min | LOW |
| 5 | Split message_registry.hpp | 2-3 hours | LOW |
| - | Testing & validation | 2-3 hours | HIGH |

**Recommended Order**:
1. Phase 1 (registry_utils) - Most useful immediately
2. Phase 2 (CommRaT::System) - Fixes WorkMailbox issues
3. Phase 3 (rename) - Clarifies intent
4. Testing - Validate no regressions
5. Phase 4 & 5 - Cleanup (can be deferred)

---

## Open Questions

1. **Backward compatibility**: Keep TypedMailbox as alias to RestrictedMailbox?
   - **Recommendation**: YES, for smooth transition
   
2. **System message registration**: Should GetData<T> messages be in CommRaT::System?
   - **Recommendation**: NO, they're type-specific, stay in UserRegistry

3. **Registry merging**: Do we need to merge registries in practice?
   - **Recommendation**: Implement if use case appears, not immediately

4. **File organization**: Keep internal/ utilities or inline in message_registry.hpp?
   - **Recommendation**: Move to internal/ for clarity

---

## Success Criteria

- [ ] Single clear pattern: `CommRaT<MessageDefs...>`
- [ ] No duplicate registry concepts
- [ ] CommRaT::System fully implemented
- [ ] TypedMailbox → RestrictedMailbox (with backward compat)
- [ ] Registry utilities available and tested
- [ ] message_registry.hpp < 300 lines
- [ ] WorkMailbox type defined and compiles
- [ ] All existing tests pass
- [ ] Documentation updated

---

## Next Immediate Action

**Start with Phase 1**: Create `registry_utils.hpp`

This provides immediate value for querying messages and will help
fix the WorkMailbox issue in Module2.

Ready to proceed?
