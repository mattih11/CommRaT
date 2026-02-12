# Registry Module Refactoring Strategy

**Date**: February 10, 2026  
**Current State**: registry_module.hpp = 1437 lines (TOO LARGE)  
**Goal**: Reduce to ~600 lines through systematic extraction  
**Branch**: feature/multi-io-modules

## Problem Statement

`registry_module.hpp` has grown too large (1437 lines) with multiple concerns mixed together:
- Type traits and metaprogramming (~200 lines)
- Multi-output infrastructure (~150 lines)
- Thread management (~100 lines)
- Mailbox lifecycle (~150 lines)
- Protocol loops (~200 lines)
- Helper functions (~100 lines)
- Core Module orchestration (~400 lines)

**User Concerns**:
- "too much code that is duplicate or something because of backwards compatibility"
- "mailbox handling, different loops etc. all in there"
- "the module again has 1500lines. thats too much"

## Extraction Strategy

### Phase 1: Type Traits Extraction
**Target**: Extract ~200 lines  
**Priority**: HIGH (foundational, low risk)  
**Estimated Time**: 1-2 hours

#### Files to Create:
1. `include/commrat/module/traits/module_types.hpp` - Type computation helpers

#### What to Extract:
```cpp
// From registry_module.hpp lines ~100-250:
- ExtractInputData<T>           // Get InputData type from InputSpec
- ExtractInputTypes<T>          // Get tuple of input types
- ExtractOutputData<T>          // Get OutputData type from OutputSpec
- OutputTypes<T>                // Get tuple of output types
- MakeMailboxSetTuple<Tuple>    // Generate MailboxSet tuple for multi-output
- MakeTypedCmdMailbox<Tuple>    // Generate typed CMD mailbox
- MakeTypedDataMailbox<Tuple>   // Generate typed DATA mailbox
- ExtractDataTypes<T>           // Get data types from InputSpec

// Type aliases to extract:
- OutputTypesTuple
- InputTypesTuple
- MailboxSetTuple
- CmdMailbox, WorkMailbox, PublishMailbox, DataMailbox types
```

#### Implementation Plan:
1. Create `module_types.hpp` with namespace `commrat::module_traits`
2. Move all type computation helpers
3. Update `registry_module.hpp` to include and use extracted types
4. Build test: `cd build && make -j$(nproc)`
5. Verify: All 20 targets build successfully

#### Success Criteria:
- ✅ All type helpers extracted to separate file
- ✅ registry_module.hpp reduced by ~200 lines
- ✅ All builds pass
- ✅ No functional changes (pure refactor)

---

### Phase 2: Multi-Output Manager Extraction
**Target**: Extract ~150 lines  
**Priority**: HIGH (isolate complexity)  
**Estimated Time**: 2-3 hours

#### Files to Create:
1. `include/commrat/module/multi_output_manager.hpp` - CRTP mixin for multi-output

#### What to Extract:
```cpp
// From registry_module.hpp:
- output_subscribers_                     // Per-output subscriber lists
- output_work_threads_                   // Per-output work threads
- add_subscriber_to_output()             // Route subscriber to correct output
- find_output_index_by_type_id()         // Map type ID to output index
- get_output_subscribers()               // Get subscribers for specific output
- start_all_mailbox_sets()               // Start all MailboxSets
- stop_all_mailbox_sets()                // Stop all MailboxSets
- spawn_all_output_work_threads()        // Create work thread per output
- start_mailbox_set<Index>()             // Start single MailboxSet
- stop_mailbox_set<Index>()              // Stop single MailboxSet
- spawn_output_work_thread<Index>()      // Create single work thread
- get_mailbox_set<Index>()               // Access MailboxSet by index
- get_cmd_mailbox<Index>()               // Access CMD mailbox by index
- get_work_mailbox<Index>()              // Access WORK mailbox by index
- get_publish_mailbox<Index>()           // Access PUBLISH mailbox by index
- get_publish_mailbox_public<Index>()    // Public accessor for publishing
- output_work_loop<Index>()              // Work loop for specific output
```

#### Implementation Plan:
1. Create `multi_output_manager.hpp` with CRTP pattern:
   ```cpp
   template<typename Derived, typename UserRegistry, typename OutputTypesTuple>
   class MultiOutputManager {
       // Friend declaration for CRTP
       Derived& derived() { return static_cast<Derived&>(*this); }
   };
   ```
2. Extract multi-output members and methods
3. Use `if constexpr (num_output_types > 1)` to conditionally enable
4. Update Module to inherit: `: public MultiOutputManager<Module<...>, ...>`
5. Add friend declaration: `friend class MultiOutputManager<...>`
6. Build test
7. Run multi-output example: `./build/example_multi_output`

#### Success Criteria:
- ✅ Multi-output logic isolated in separate mixin
- ✅ registry_module.hpp reduced by ~150 lines
- ✅ Multi-output example works correctly
- ✅ Type-safe publishing validated (TempReceiver gets only Temp, PressureReceiver gets only Pressure)
- ✅ Single-output backward compatibility maintained

---

### Phase 3: Protocol Loops Extraction
**Target**: Extract ~100 lines  
**Priority**: MEDIUM (clean separation)  
**Estimated Time**: 1-2 hours

#### Files to Create:
1. `include/commrat/module/loops/protocol_loops.hpp` - CRTP mixin for protocol handling

#### What to Extract:
```cpp
// From registry_module.hpp lines ~1100-1250:
- command_loop()                    // Handle user commands (CMD mailbox)
- work_loop()                       // Handle subscription (WORK mailbox, single-output)
- output_work_loop<Index>()         // Already in MultiOutputManager
- handle_user_command<CmdT>()       // Dispatch user commands
```

#### Implementation Plan:
1. Create `protocol_loops.hpp` parallel to existing `loop_executor.hpp`
2. Extract command_loop() and work_loop()
3. Keep output_work_loop<Index>() in MultiOutputManager (tightly coupled)
4. Use CRTP pattern like LoopExecutor
5. Update Module inheritance
6. Build and test subscription protocol

#### Success Criteria:
- ✅ Protocol loops separated from data processing loops
- ✅ registry_module.hpp reduced by ~100 lines
- ✅ Subscription protocol still works
- ✅ Command handling works
- ✅ Clean parallel: ProtocolLoops (CMD/WORK) vs LoopExecutor (DATA processing)

---

### Phase 4: Lifecycle Manager Extraction ⚠️ OPTIONAL
**Target**: Extract ~150 lines  
**Priority**: LOW (complex, lower benefit)  
**Estimated Time**: 2-3 hours

**DEFER THIS**: More complex extraction with lower immediate benefit.

#### Rationale for Deferring:
- Thread members accessed throughout Module
- start()/stop() coordinate multiple concerns
- Code is mechanical but not particularly complex
- Better to stabilize after Phases 1-3 first

#### Future Considerations:
If we do this later, create:
- `include/commrat/module/lifecycle_manager.hpp`
- Extract: data_thread_, command_thread_, work_thread_ management
- Extract: Thread spawning/joining coordination
- Coordinate with MultiOutputManager for output_work_threads_

---

## Tracking Checklist

### Phase 1: Type Traits Extraction
- [x] Create `include/commrat/module/traits/module_types.hpp`
- [x] Extract type computation helpers (ExtractInputData, ExtractOutputData, etc.)
- [x] Extract type aliases (OutputTypesTuple, InputTypesTuple, etc.)
- [x] Update registry_module.hpp includes
- [x] Update registry_module.hpp to use extracted types
- [x] Build test: `cd build && make -j$(nproc)`
- [x] Verify line count: `wc -l include/commrat/registry_module.hpp`
- [x] Git commit: "refactor: extract type traits from registry_module (Phase 1)"

**COMPLETED**: February 10, 2026
- registry_module.hpp: 1437 → 1298 lines (139 lines removed, 9.7% reduction)
- module_types.hpp: 243 lines (new file)
- Net change: -139 lines from registry_module.hpp
- All 20 targets building successfully
- Multi-output example tested and working

### Phase 2: Multi-Output Manager
- [ ] Create `include/commrat/module/multi_output_manager.hpp`
- [ ] Implement CRTP base class structure
- [ ] Extract output_subscribers_ member
- [ ] Extract output_work_threads_ member
- [ ] Extract subscriber management methods
- [ ] Extract mailbox set lifecycle methods
- [ ] Extract mailbox accessors (get_cmd_mailbox<Index>, etc.)
- [ ] Extract output_work_loop<Index>()
- [ ] Update Module to inherit from MultiOutputManager
- [ ] Add friend declaration
- [ ] Build test
- [ ] Run multi-output example: `cd build && ./example_multi_output`
- [ ] Verify type-safe publishing (TempReceiver only Temp, PressureReceiver only Pressure)
- [ ] Verify line count reduction
- [ ] Git commit: "refactor: extract multi-output manager from registry_module (Phase 2)"

### Phase 3: Protocol Loops
- [ ] Create `include/commrat/module/loops/protocol_loops.hpp`
- [ ] Implement CRTP base class (parallel to loop_executor.hpp)
- [ ] Extract command_loop() method
- [ ] Extract work_loop() method
- [ ] Extract handle_user_command<CmdT>() method
- [ ] Update Module to inherit from ProtocolLoops
- [ ] Add friend declaration
- [ ] Build test
- [ ] Test subscription protocol (producer-consumer subscription)
- [ ] Test command handling
- [ ] Verify line count reduction
- [ ] Git commit: "refactor: extract protocol loops from registry_module (Phase 3)"

### Final Verification
- [ ] Total line count: `wc -l include/commrat/registry_module.hpp` (target: ~600-700 lines)
- [ ] Build all targets: `cd build && make -j$(nproc)`
- [ ] Run all examples:
  - [ ] `./build/example_sender`
  - [ ] `./build/example_receiver`
  - [ ] `./build/example_multi_output`
- [ ] Verify backward compatibility (single-output modules)
- [ ] Verify multi-output functionality
- [ ] Update documentation if needed
- [ ] Final git commit: "refactor: complete registry_module refactoring (Phases 1-3)"

---

## Expected Results

### Before Refactoring:
```
include/commrat/registry_module.hpp: 1437 lines
```

### After Phase 1:
```
include/commrat/registry_module.hpp: ~1237 lines (-200)
include/commrat/module/traits/module_types.hpp: +200 lines (NEW)
```

### After Phase 2:
```
include/commrat/registry_module.hpp: ~1087 lines (-150)
include/commrat/module/multi_output_manager.hpp: +150 lines (NEW)
```

### After Phase 3:
```
include/commrat/registry_module.hpp: ~987 lines (-100)
include/commrat/module/loops/protocol_loops.hpp: +100 lines (NEW)
```

### Final State:
```
include/commrat/registry_module.hpp: ~600-700 lines (58% reduction!)
+ 3 new focused, testable, reusable modules
```

---

## Risk Mitigation

### Low Risk Items (Do First):
1. ✅ Type traits extraction - Pure metaprogramming, no runtime behavior
2. ✅ Multi-output manager - Clear boundary, already conditionally compiled

### Medium Risk Items:
3. ⚠️ Protocol loops - Need to maintain CRTP friendships correctly

### High Risk Items (Defer):
4. ❌ Lifecycle manager - Complex interdependencies, save for later

### Testing Strategy:
- After each phase: Full build (`make -j$(nproc)`)
- After Phase 2: Run multi-output example specifically
- After Phase 3: Test subscription protocol end-to-end
- Final: Run all examples and verify behavior unchanged

---

## Notes

### Backward Compatibility:
- All refactoring must maintain existing API
- Single-output modules should see no changes
- Multi-output modules should work identically
- No user-facing API changes

### CRTP Pattern:
All extracted mixins follow the existing pattern:
```cpp
template<typename Derived, ...>
class ExtractedMixin {
    Derived& derived() { return static_cast<Derived&>(*this); }
    const Derived& derived() const { return static_cast<const Derived&>(*this); }
};

// Module inherits:
class Module : public ExtractedMixin<Module<...>, ...> {
    friend class ExtractedMixin<...>;
};
```

### Code Style:
- Follow existing CommRaT conventions
- Real-time safe (no allocations in hot paths)
- Compile-time dispatch where possible
- Clear documentation comments

---

## Expected Module Structure After Refactoring

### Current Structure (1437 lines)
```cpp
// include/commrat/registry_module.hpp (BEFORE REFACTORING)
template<typename UserRegistry, typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
class Module 
    : public ContinuousProcessorBase<...>
    , public MultiOutputProcessorBase<...>
    , public SingleOutputProcessorBase<...>
    , public ResolveMultiInputBase<...>::type
    , public SubscriberManager
    , public LoopExecutor<Module<...>>
    , public InputMetadataAccessors<Module<...>>
{
    friend class LoopExecutor<...>;
    friend class InputMetadataAccessors<...>;
    
private:
    // Type traits (~200 lines) - TO BE EXTRACTED
    using OutputSpec = NormalizeOutput_t<OutputSpec_>;
    using InputSpec = NormalizeInput_t<InputSpec_>;
    template<typename T> struct ExtractInputData { ... };
    template<typename T> struct ExtractInputTypes { ... };
    template<typename T> struct ExtractOutputData { ... };
    template<typename T> struct OutputTypes { ... };
    template<typename... Ts> struct MakeMailboxSetTuple { ... };
    template<typename... Ts> struct MakeTypedCmdMailbox { ... };
    template<typename Tuple> struct MakeTypedDataMailbox { ... };
    using OutputTypesTuple = ...;
    using InputTypesTuple = ...;
    using MailboxSetTuple = ...;
    using CmdMailbox = ...;
    using WorkMailbox = ...;
    using PublishMailbox = ...;
    using DataMailbox = ...;
    
protected:
    ModuleConfig config_;
    
    // Mailbox infrastructure (~50 lines)
    std::conditional_t<...> mailbox_infrastructure_;
    CmdMailbox& cmd_mailbox() { ... }
    WorkMailbox& work_mailbox() { ... }
    PublishMailbox& publish_mailbox() { ... }
    template<std::size_t Index> auto& get_mailbox_set() { ... }
    
    // Multi-output infrastructure (~150 lines) - TO BE EXTRACTED
    std::vector<std::vector<uint32_t>> output_subscribers_;
    std::vector<std::thread> output_work_threads_;
    void add_subscriber_to_output(uint32_t subscriber_id) { ... }
    std::size_t find_output_index_by_type_id(uint16_t type_id_low) const { ... }
    std::vector<uint32_t> get_output_subscribers(std::size_t idx) const { ... }
    template<std::size_t Index> CmdMailbox& get_cmd_mailbox() { ... }
    template<std::size_t Index> WorkMailbox& get_work_mailbox() { ... }
    template<std::size_t Index> PublishMailbox& get_publish_mailbox() { ... }
    template<std::size_t Index> auto& get_publish_mailbox_public() { ... }
    
    // Metadata storage
    InputMetadataStorage<InputTypesTuple> input_metadata_;
    
    // Lifecycle hooks (user overrides)
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_command(...) {}
    
    // Processing hooks (user overrides)
    virtual OutputData process() { ... }
    virtual OutputData process_continuous(const InputData&) { ... }
    virtual void process(Ts&... outputs) { ... }
    virtual OutputData process_multi_input(const Ts&...) { ... }
    
public:
    Module(ModuleConfig config) { ... }
    
    // Lifecycle management (~200 lines)
    void start() {
        // Thread spawning
        // Mailbox startup
        // Subscription logic
    }
    void stop() {
        // Thread joining
        // Mailbox shutdown
        // Cleanup
    }
    
    template<std::size_t... Is>
    void start_all_mailbox_sets(std::index_sequence<Is...>) { ... }
    template<std::size_t Index>
    void start_mailbox_set() { ... }
    template<std::size_t... Is>
    void spawn_all_output_work_threads(std::index_sequence<Is...>) { ... }
    template<std::size_t Index>
    void spawn_output_work_thread() { ... }
    template<std::size_t... Is>
    void stop_all_mailbox_sets(std::index_sequence<Is...>) { ... }
    template<std::size_t Index>
    void stop_mailbox_set() { ... }
    
protected:
    // Subscription protocol (delegated)
    void subscribe_to_source(...) { ... }
    void handle_subscribe_request(...) { ... }
    void handle_subscribe_reply(...) { ... }
    
    // Publishing (delegated)
    void publish_to_subscribers(...) { ... }
    void publish_multi_outputs(...) { ... }
    
    // Main loops (~200 lines) - TO BE EXTRACTED
    void command_loop() {
        // BLOCKING receive on CMD mailbox
        // Dispatch user commands
    }
    void work_loop() {
        // BLOCKING receive on WORK mailbox (single-output)
        // Handle subscription protocol
    }
    template<std::size_t Index>
    void output_work_loop() {
        // BLOCKING receive on WORK mailbox (multi-output)
        // Handle subscription protocol per output
    }
    template<typename CmdT>
    void handle_user_command(const CmdT& cmd) { ... }
    
    // Helper functions (~100 lines)
    static constexpr size_t get_primary_input_index() { ... }
    template<size_t Index>
    auto create_historical_mailbox_for_input() { ... }
    // ... more helpers ...
    
private:
    // Thread members
    std::atomic<bool> running_{false};
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    std::optional<std::thread> work_thread_;
    std::vector<std::thread> secondary_input_threads_;
    
    // Mailbox members
    std::optional<DataMailbox*> data_mailbox_;
    std::vector<std::unique_ptr<HistoricalMailbox<UserRegistry, InputType>>> input_mailboxes_;
};
```

### Target Structure After Phases 1-3 (~600-700 lines)
```cpp
// include/commrat/registry_module.hpp (AFTER REFACTORING)
#pragma once

#include "commrat/module/traits/module_types.hpp"       // NEW: Phase 1
#include "commrat/module/multi_output_manager.hpp"      // NEW: Phase 2
#include "commrat/module/loops/protocol_loops.hpp"      // NEW: Phase 3
#include "commrat/module/loops/loop_executor.hpp"       // Existing
#include "commrat/module/metadata/input_metadata_accessors.hpp"  // Existing
// ... other includes ...

namespace commrat {

/**
 * @brief Modern C++20 module base using MessageRegistry
 * 
 * ARCHITECTURE:
 * - Type computation: Extracted to module_types.hpp
 * - Multi-output management: Extracted to MultiOutputManager mixin
 * - Protocol loops: Extracted to ProtocolLoops mixin
 * - Data processing loops: Already in LoopExecutor mixin
 * - Subscription protocol: Already in SubscriptionProtocol delegation
 * - Publishing: Already in PublishingOperations delegation
 * 
 * @tparam UserRegistry The application's complete MessageRegistry
 * @tparam OutputSpec_ Output specification (normalized to Output<T>/Outputs<Ts...>)
 * @tparam InputSpec_ Input specification (normalized Input<T>/Inputs<Ts...>/PeriodicInput/LoopInput)
 * @tparam CommandTypes Optional variadic command types this module handles
 */
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class Module 
    : public ContinuousProcessorBase<...>              // Existing: conditional process_continuous()
    , public MultiOutputProcessorBase<...>              // Existing: conditional multi-output process()
    , public SingleOutputProcessorBase<...>             // Existing: conditional single-output process()
    , public ResolveMultiInputBase<...>::type           // Existing: conditional multi-input process()
    , public SubscriberManager                          // Existing: subscriber management base
    , public LoopExecutor<Module<...>>                  // Existing: periodic/free/continuous/multi-input loops
    , public InputMetadataAccessors<Module<...>>        // Existing: get_input_metadata<Index>()
    , public MultiOutputManager<Module<...>, UserRegistry, OutputTypesTuple>  // NEW Phase 2
    , public ProtocolLoops<Module<...>>                 // NEW Phase 3
{
    // Friend declarations for CRTP mixins
    friend class LoopExecutor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class InputMetadataAccessors<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, OutputTypesTuple>;  // NEW
    friend class ProtocolLoops<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;  // NEW
    
private:
    // ========================================================================
    // Type Definitions (extracted to module_types.hpp in Phase 1)
    // ========================================================================
    
    // Import type computations from module_types.hpp
    using ModuleTypes = module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>;
    
    using OutputSpec = typename ModuleTypes::OutputSpec;
    using InputSpec = typename ModuleTypes::InputSpec;
    using OutputTypesTuple = typename ModuleTypes::OutputTypesTuple;
    using InputTypesTuple = typename ModuleTypes::InputTypesTuple;
    using MailboxSetTuple = typename ModuleTypes::MailboxSetTuple;
    using CmdMailbox = typename ModuleTypes::CmdMailbox;
    using WorkMailbox = typename ModuleTypes::WorkMailbox;
    using PublishMailbox = typename ModuleTypes::PublishMailbox;
    using DataMailbox = typename ModuleTypes::DataMailbox;
    
    static constexpr size_t num_output_types = ModuleTypes::num_output_types;
    static constexpr size_t InputCount = ModuleTypes::InputCount;
    static constexpr bool use_mailbox_sets = ModuleTypes::use_mailbox_sets;
    
public:
    // Public type aliases (user-visible)
    using OutputData = typename ModuleTypes::OutputData;
    using InputData = typename ModuleTypes::InputData;
    
    static constexpr bool has_continuous_input = ModuleTypes::has_continuous_input;
    static constexpr bool has_periodic_input = ModuleTypes::has_periodic_input;
    static constexpr bool has_loop_input = ModuleTypes::has_loop_input;
    static constexpr bool has_multi_input = ModuleTypes::has_multi_input;
    static constexpr bool has_multi_output = ModuleTypes::has_multi_output;
    
    // ========================================================================
    // Constructor
    // ========================================================================
    
    Module(ModuleConfig config)
        : config_(std::move(config))
        , mailbox_infrastructure_(create_mailbox_infrastructure())
    {
        // Initialize mailbox infrastructure based on configuration
    }
    
    // ========================================================================
    // Lifecycle Management (Public API)
    // ========================================================================
    
    /**
     * @brief Start the module (spawn threads, start mailboxes, subscribe)
     * 
     * Coordinates:
     * - Mailbox initialization (via MultiOutputManager if multi-output)
     * - Thread spawning (via ProtocolLoops and LoopExecutor)
     * - Subscription to input sources
     */
    void start() {
        if (running_) return;
        
        // Start mailbox infrastructure
        if constexpr (use_mailbox_sets) {
            this->start_all_mailbox_sets(std::make_index_sequence<num_output_types>{});
        } else {
            // Single-output: start traditional mailboxes
            start_single_output_mailboxes();
        }
        
        // Start multi-input mailboxes if needed
        if constexpr (has_multi_input) {
            start_input_mailboxes();
        }
        
        running_ = true;
        on_start();
        
        // Spawn protocol threads (command + work loops)
        this->spawn_protocol_threads();  // NEW: From ProtocolLoops
        
        // Subscribe to sources
        if constexpr (has_multi_input) {
            subscribe_to_all_sources();
        } else if constexpr (has_continuous_input) {
            if (config_.source_system_id && config_.source_instance_id) {
                subscribe_to_source(*config_.source_system_id, *config_.source_instance_id);
            }
        }
        
        // Spawn data processing thread
        this->spawn_data_thread();  // From LoopExecutor
        
        // Spawn secondary input threads if multi-input
        if constexpr (has_multi_input) {
            start_secondary_input_threads();
        }
    }
    
    /**
     * @brief Stop the module (unsubscribe, join threads, stop mailboxes)
     */
    void stop() {
        if (!running_) return;
        
        on_stop();
        
        // Unsubscribe from sources
        if constexpr (has_multi_input) {
            for (const auto& source : config_.input_sources) {
                unsubscribe_from_multi_input_source(source);
            }
        } else if constexpr (has_continuous_input) {
            if (config_.source_system_id && config_.source_instance_id) {
                unsubscribe_from_source(*config_.source_system_id, *config_.source_instance_id);
            }
        }
        
        running_ = false;
        
        // Join all threads
        this->join_all_threads();  // Coordinates LoopExecutor + ProtocolLoops + MultiOutputManager
        
        // Stop mailbox infrastructure
        if constexpr (use_mailbox_sets) {
            this->stop_all_mailbox_sets(std::make_index_sequence<num_output_types>{});
        } else {
            stop_single_output_mailboxes();
        }
        
        if constexpr (has_multi_input) {
            stop_input_mailboxes();
        }
    }
    
    // ========================================================================
    // Lifecycle Hooks (Override These in Derived Classes)
    // ========================================================================
    
protected:
    virtual void on_start() {}
    virtual void on_stop() {}
    
    /**
     * @brief Handle a specific command type (override in derived class)
     * 
     * Only called for commands declared in CommandTypes template parameter.
     * 
     * @tparam CmdT Command payload type
     * @param cmd Command payload
     */
    template<typename CmdT>
    void on_command(const CmdT& cmd) {
        // Default: no-op - override in derived classes
    }
    
    // ========================================================================
    // Processing Hooks (Override Based on InputMode)
    // ========================================================================
    // 
    // These are provided by processor base classes (ContinuousProcessorBase, etc.)
    // Users override the appropriate method based on their InputSpec:
    // 
    // - PeriodicInput:  OutputData process()
    // - LoopInput:      OutputData process()
    // - Input<T>:       OutputData process_continuous(const InputData&)
    // - Inputs<Ts...>:  OutputData process_multi_input(const Ts&...)
    // - Multi-output:   void process(T1& out1, T2& out2, ...)
    //
    
    // ========================================================================
    // Configuration & State
    // ========================================================================
    
    ModuleConfig config_;
    std::atomic<bool> running_{false};
    
    // ========================================================================
    // Mailbox Infrastructure
    // ========================================================================
    
    std::conditional_t<
        use_mailbox_sets,
        MailboxSetTuple,  // Multi-output: tuple of MailboxSets
        std::tuple<CmdMailbox, WorkMailbox, PublishMailbox>  // Single-output
    > mailbox_infrastructure_;
    
    // Backward compatible accessors (single-output)
    CmdMailbox& cmd_mailbox() {
        if constexpr (!use_mailbox_sets) {
            return std::get<0>(mailbox_infrastructure_);
        } else {
            return *std::get<0>(mailbox_infrastructure_).cmd;
        }
    }
    
    WorkMailbox& work_mailbox() {
        if constexpr (!use_mailbox_sets) {
            return std::get<1>(mailbox_infrastructure_);
        } else {
            return *std::get<0>(mailbox_infrastructure_).work;
        }
    }
    
    PublishMailbox& publish_mailbox() {
        static_assert(!use_mailbox_sets, "Use get_publish_mailbox_public<Index>() for multi-output");
        return std::get<2>(mailbox_infrastructure_);
    }
    
    // Multi-output accessors provided by MultiOutputManager mixin:
    // - template<std::size_t Index> auto& get_mailbox_set()
    // - template<std::size_t Index> CmdMailbox& get_cmd_mailbox()
    // - template<std::size_t Index> WorkMailbox& get_work_mailbox()
    // - template<std::size_t Index> PublishMailbox& get_publish_mailbox()
    // - template<std::size_t Index> auto& get_publish_mailbox_public()
    
    // ========================================================================
    // Input Metadata Storage (Phase 6.10)
    // ========================================================================
    
    InputMetadataStorage<InputTypesTuple> input_metadata_;
    
    // ========================================================================
    // Data Mailbox (for continuous/multi-input)
    // ========================================================================
    
    std::optional<DataMailbox*> data_mailbox_;
    std::vector<std::unique_ptr<HistoricalMailbox<UserRegistry, InputType>>> input_mailboxes_;
    
    // ========================================================================
    // Thread Members
    // ========================================================================
    // 
    // Managed by mixins:
    // - data_thread_: LoopExecutor
    // - command_thread_, work_thread_: ProtocolLoops
    // - output_work_threads_: MultiOutputManager
    // - secondary_input_threads_: Module (multi-input specific)
    //
    
    std::optional<std::thread> data_thread_;           // Data processing (periodic/continuous/multi-input)
    std::optional<std::thread> command_thread_;        // User commands
    std::optional<std::thread> work_thread_;           // Subscription protocol (single-output)
    std::vector<std::thread> secondary_input_threads_; // Multi-input secondary inputs
    
    // output_work_threads_ moved to MultiOutputManager mixin
    
    // ========================================================================
    // Subscription Protocol (Delegation to SubscriptionProtocol)
    // ========================================================================
    
    void subscribe_to_source(uint32_t system_id, uint32_t instance_id);
    void unsubscribe_from_source(uint32_t system_id, uint32_t instance_id);
    void handle_subscribe_request(const SubscribeRequestType& req);
    void handle_subscribe_reply(const SubscribeReplyType& reply);
    void handle_unsubscribe_request(const UnsubscribeRequestType& req);
    
    // Multi-input subscription
    void subscribe_to_all_sources();
    void unsubscribe_from_multi_input_source(const ModuleConfig::InputSource& source);
    
    // ========================================================================
    // Publishing (Delegation to PublishingOperations)
    // ========================================================================
    
    void publish_to_subscribers(OutputData& output);
    void publish_multi_outputs(OutputTypesTuple& outputs);
    void publish_multi_outputs_with_timestamp(uint64_t timestamp, OutputTypesTuple& outputs);
    
    // ========================================================================
    // Helper Functions
    // ========================================================================
    
private:
    static constexpr size_t get_primary_input_index();
    
    template<size_t Index>
    auto create_historical_mailbox_for_input();
    
    void start_single_output_mailboxes();
    void stop_single_output_mailboxes();
    
    void start_input_mailboxes();
    void stop_input_mailboxes();
    void start_secondary_input_threads();
    
    void join_all_threads() {
        // Join data thread (LoopExecutor)
        if (data_thread_ && data_thread_->joinable()) {
            data_thread_->join();
        }
        
        // Join secondary input threads
        for (auto& thread : secondary_input_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        // Join protocol threads (ProtocolLoops)
        this->join_protocol_threads();
        
        // Join output work threads (MultiOutputManager)
        if constexpr (use_mailbox_sets) {
            this->join_output_work_threads();
        }
    }
    
    auto create_mailbox_infrastructure() {
        if constexpr (use_mailbox_sets) {
            return create_mailbox_set_tuple(std::make_index_sequence<num_output_types>{});
        } else {
            return create_single_output_mailboxes();
        }
    }
    
    template<std::size_t... Is>
    auto create_mailbox_set_tuple(std::index_sequence<Is...>) {
        return std::make_tuple(create_mailbox_set<Is>()...);
    }
    
    template<std::size_t Index>
    auto create_mailbox_set() {
        using OutputType = std::tuple_element_t<Index, OutputTypesTuple>;
        return MailboxSet<UserRegistry, OutputType, CommandTypes...>(config_);
    }
    
    auto create_single_output_mailboxes() {
        uint32_t base_addr = calculate_base_address(config_.system_id, config_.instance_id);
        return std::make_tuple(
            CmdMailbox(base_addr + static_cast<uint32_t>(MailboxType::CMD)),
            WorkMailbox(base_addr + static_cast<uint32_t>(MailboxType::WORK)),
            PublishMailbox(base_addr + static_cast<uint32_t>(MailboxType::PUBLISH))
        );
    }
};

}  // namespace commrat
```

### Key Differences Summary

| Aspect | Before (1437 lines) | After (600-700 lines) | Change |
|--------|---------------------|----------------------|--------|
| **Type Traits** | Inline in Module (~200 lines) | Extracted to `module_types.hpp` | ✅ Separated |
| **Multi-Output** | Mixed in Module (~150 lines) | Extracted to `MultiOutputManager` | ✅ Mixin |
| **Protocol Loops** | Inline in Module (~100 lines) | Extracted to `ProtocolLoops` | ✅ Mixin |
| **Data Processing Loops** | Already in `LoopExecutor` | No change | ✅ Already separated |
| **Subscription** | Already delegated | No change | ✅ Already separated |
| **Publishing** | Already delegated | No change | ✅ Already separated |
| **Lifecycle** | Inline (~200 lines) | Stays in Module (orchestration) | ⚠️ Phase 4 (deferred) |
| **Core Orchestration** | Mixed with everything | Clean, focused (~400 lines) | ✅ Clear responsibility |

### Benefits of New Structure

1. **Single Responsibility**: Each file has one clear purpose
2. **Testability**: Each mixin can be tested independently
3. **Reusability**: Mixins can be composed for different module types
4. **Readability**: Module.hpp shows high-level architecture clearly
5. **Maintainability**: Changes to multi-output don't touch single-output code
6. **Compile-time Cost**: No change - still compile-time polymorphism via CRTP
7. **Runtime Cost**: Zero - same generated code, just better organized

## Current Status

**STATUS**: ✅ READY TO START  
**NEXT ACTION**: Begin Phase 1 - Type Traits Extraction  
**BLOCKER**: None - strategy approved by user

