# Module Base Refactoring Proposal

**Date**: February 11, 2026  
**Status**: PROPOSAL - Awaiting approval  
**Scope**: Major architectural refactoring of Module inheritance hierarchy

## Executive Summary

Replace the current monolithic Module class (13+ base class inheritance) with a two-layer architecture:
1. **Clean user-facing Module** (thin wrapper, documentation only)
2. **Specialized ModuleBase variants** (8 implementations, each inheriting only what it needs)

**Expected Benefits**:
- Eliminate all `void/void` template specialization workarounds
- Reduce compilation time (fewer template instantiations)
- Clearer error messages (only relevant bases instantiated)
- Easier maintenance (each mode isolated)
- Better documentation (clean separation of user API from implementation)

**Risk**: High complexity migration, but systematic and testable

---

## Current Architecture Problems

### Problem 1: Unconditional Base Inheritance

**Current** (`registry_module.hpp` lines 86-138):
```cpp
class Module 
    : public ContinuousProcessorBase<InputData, OutputData>      // ALWAYS inherited
    , public MultiOutputProcessorBase<OutputTuple, InputData>    // ALWAYS inherited
    , public SingleOutputProcessorBase<InputData, OutputData>    // ALWAYS inherited
    , public ResolveMultiInputBase<InputSpec, OutputSpec>::type  // Conditional
    , public MultiOutputManager<...>                             // ALWAYS inherited
    , public LoopExecutor<...>                                   // ALWAYS inherited
    // ... 7 more bases
```

**Problem**: A periodic single-output module inherits:
- `ContinuousProcessorBase<void, TempData>` - provides `process(const void&, TempData&)` - ILLEGAL!
- `MultiOutputProcessorBase<std::tuple<TempData>, void>` - provides `process(TempData&)` - NOT NEEDED!
- `SingleOutputProcessorBase<void, TempData>` - provides `process(TempData&)` - ONLY ONE NEEDED!

**Workaround Required**: Add `ContinuousProcessorBase<void, void>` empty specialization (just added in Phase 6.10)

### Problem 2: Template Type Explosion

Every CRTP mixin repeats: `Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>`

**Current count**: 10+ repetitions in inheritance list alone, 20+ in friend declarations

**Impact**:
- Long compilation times (each instantiation expensive)
- Error messages 500+ lines long (template backtraces through all bases)
- Hard to read and maintain

### Problem 3: Mixed User API and Implementation

**Current** (`registry_module.hpp` 575 lines):
- Lines 1-85: Documentation + imports
- Lines 86-138: 13 base class inheritance declarations
- Lines 139-154: Friend declarations (implementation detail)
- Lines 155-223: Type definitions (implementation detail)
- Lines 224-575: Member variables, constructors, methods (mix of public API and private implementation)

**Problem**: User must wade through 575 lines mixing API with implementation details

### Problem 4: Conditional Compilation Complexity

```cpp
, public std::conditional_t<
    module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::has_multi_input,
    MultiInputInfrastructure<Module<...>, UserRegistry, ...InputTypesTuple, InputCount>,
    EmptyBase2  // Need empty base class!
  >
```

**Problems**:
- Need `EmptyBase2`, `EmptyBase3` dummy classes
- Conditional logic scattered across inheritance list
- Hard to reason about which bases are actually present

---

## Proposed Architecture

### Layer 1: User-Facing Module (Thin Wrapper)

**File**: `include/commrat/module.hpp` (~100 lines, mostly documentation)

```cpp
#pragma once

#include "commrat/module/base/module_base_selector.hpp"

namespace commrat {

/**
 * @brief Modern C++20 real-time messaging module
 * 
 * Provides type-safe, compile-time message passing with automatic
 * subscription management and timestamp propagation.
 * 
 * @tparam Registry MessageRegistry with all application message types
 * @tparam OutputSpec Output<T>, Outputs<T,U,V>, or raw type T
 * @tparam InputSpec Input<T>, Inputs<T,U,V>, PeriodicInput, or LoopInput
 * @tparam CommandTypes Optional command types this module handles
 * 
 * ## Processing Signatures (Override in derived class)
 * 
 * **Periodic/Loop single output**:
 * ```cpp
 * void process(OutputData& output) override;
 * ```
 * 
 * **Continuous single input/output**:
 * ```cpp
 * void process(const InputData& input, OutputData& output) override;
 * ```
 * 
 * **Multi-output periodic/loop**:
 * ```cpp
 * void process(OutputData1& out1, OutputData2& out2, ...) override;
 * ```
 * 
 * **Multi-input single output**:
 * ```cpp
 * void process(const Input1&, const Input2&, OutputData& output) override;
 * ```
 * 
 * **Multi-input multi-output**:
 * ```cpp
 * void process(const Input1&, const Input2&, Output1& out1, Output2& out2) override;
 * ```
 * 
 * ## Lifecycle Hooks (Optional)
 * 
 * ```cpp
 * void on_init() override;    // Before start()
 * void on_start() override;   // After threads launched
 * void on_stop() override;    // Before cleanup
 * void on_cleanup() override; // Final cleanup
 * ```
 * 
 * ## Example Usage
 * 
 * ```cpp
 * using MyApp = CommRaT<
 *     Message::Data<TemperatureData>,
 *     Message::Data<PressureData>
 * >;
 * 
 * class SensorModule : public MyApp::Module<Output<TemperatureData>, PeriodicInput> {
 * protected:
 *     void process(TemperatureData& output) override {
 *         output.temperature_c = read_sensor();
 *     }
 * };
 * ```
 */
template<typename Registry,
         typename OutputSpec,
         typename InputSpec,
         typename... CommandTypes>
class Module : public SelectModuleBase<Registry, OutputSpec, InputSpec, CommandTypes...> {
public:
    using Base = SelectModuleBase<Registry, OutputSpec, InputSpec, CommandTypes...>;
    using Base::Base;  // Inherit constructors
    
    // Re-export user-facing type aliases
    using OutputData = typename Base::OutputData;
    using InputData = typename Base::InputData;
    
    // Re-export configuration flags
    static constexpr bool has_periodic_input = Base::has_periodic_input;
    static constexpr bool has_continuous_input = Base::has_continuous_input;
    static constexpr bool has_loop_input = Base::has_loop_input;
    static constexpr bool has_multi_input = Base::has_multi_input;
    static constexpr bool has_multi_output = Base::has_multi_output;
};

} // namespace commrat
```

**Analysis**:
- **Lines**: ~100 (vs current 575)
- **User sees**: Only what they need - template parameters, process() signatures, examples
- **Implementation**: Completely hidden in base selector

### Layer 2: Base Selector (Compile-Time Dispatch)

**File**: `include/commrat/module/base/module_base_selector.hpp` (~80 lines)

```cpp
#pragma once

#include "commrat/module/base/periodic_single_base.hpp"
#include "commrat/module/base/periodic_multi_base.hpp"
#include "commrat/module/base/continuous_single_base.hpp"
#include "commrat/module/base/continuous_multi_base.hpp"
#include "commrat/module/base/multi_input_single_base.hpp"
#include "commrat/module/base/multi_input_multi_base.hpp"
#include "commrat/module/base/loop_single_base.hpp"
#include "commrat/module/base/loop_multi_base.hpp"

namespace commrat {

// Forward declarations for base classes
template<typename R, typename O, typename I, typename... C> class PeriodicSingleOutputBase;
template<typename R, typename O, typename I, typename... C> class PeriodicMultiOutputBase;
template<typename R, typename O, typename I, typename... C> class ContinuousSingleOutputBase;
template<typename R, typename O, typename I, typename... C> class ContinuousMultiOutputBase;
template<typename R, typename O, typename I, typename... C> class MultiInputSingleOutputBase;
template<typename R, typename O, typename I, typename... C> class MultiInputMultiOutputBase;
template<typename R, typename O, typename I, typename... C> class LoopSingleOutputBase;
template<typename R, typename O, typename I, typename... C> class LoopMultiOutputBase;

// Helper traits for input mode detection
template<typename T> struct is_periodic_input : std::is_same<T, PeriodicInput> {};
template<typename T> struct is_loop_input : std::is_same<T, LoopInput> {};
template<typename T> struct is_continuous_input : std::false_type {};
template<typename T> struct is_continuous_input<Input<T>> : std::true_type {};
template<typename T> struct is_multi_input : std::false_type {};
template<typename... Ts> struct is_multi_input<Inputs<Ts...>> : std::true_type {};

template<typename T> inline constexpr bool is_periodic_input_v = is_periodic_input<T>::value;
template<typename T> inline constexpr bool is_loop_input_v = is_loop_input<T>::value;
template<typename T> inline constexpr bool is_continuous_input_v = is_continuous_input<T>::value;
template<typename T> inline constexpr bool is_multi_input_v = is_multi_input<T>::value;

// Helper traits for output mode detection
template<typename T> struct is_single_output : std::true_type {};  // Default
template<typename T> struct is_single_output<Output<T>> : std::true_type {};
template<typename... Ts> struct is_single_output<Outputs<Ts...>> : std::false_type {};

template<typename T> inline constexpr bool is_single_output_v = is_single_output<T>::value;
template<typename T> inline constexpr bool is_multi_output_v = !is_single_output_v<T>;

/**
 * @brief Select appropriate ModuleBase variant based on I/O specs
 * 
 * Compile-time selection of 8 possible base implementations:
 * - PeriodicInput + single output   → PeriodicSingleOutputBase
 * - PeriodicInput + multi-output    → PeriodicMultiOutputBase
 * - Input<T> + single output        → ContinuousSingleOutputBase
 * - Input<T> + multi-output         → ContinuousMultiOutputBase
 * - Inputs<T,U,V> + single output   → MultiInputSingleOutputBase
 * - Inputs<T,U,V> + multi-output    → MultiInputMultiOutputBase
 * - LoopInput + single output       → LoopSingleOutputBase
 * - LoopInput + multi-output        → LoopMultiOutputBase
 */
template<typename Registry, typename OutputSpec, typename InputSpec, typename... CommandTypes>
using SelectModuleBase = 
    typename std::conditional_t<is_periodic_input_v<InputSpec>,
        std::conditional_t<is_single_output_v<OutputSpec>,
            PeriodicSingleOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
            PeriodicMultiOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>
        >,
    typename std::conditional_t<is_loop_input_v<InputSpec>,
        std::conditional_t<is_single_output_v<OutputSpec>,
            LoopSingleOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
            LoopMultiOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>
        >,
    typename std::conditional_t<is_continuous_input_v<InputSpec>,
        std::conditional_t<is_single_output_v<OutputSpec>,
            ContinuousSingleOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
            ContinuousMultiOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>
        >,
    typename std::conditional_t<is_multi_input_v<InputSpec>,
        std::conditional_t<is_single_output_v<OutputSpec>,
            MultiInputSingleOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
            MultiInputMultiOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>
        >,
        void  // Should never reach here - static_assert in Module
    >>>>;

} // namespace commrat
```

**Analysis**:
- Clean compile-time dispatch
- No runtime overhead
- Each mode gets exactly the base it needs

### Layer 3: Specialized Base Implementations (8 Files)

#### Example 1: PeriodicSingleOutputBase

**File**: `include/commrat/module/base/periodic_single_base.hpp` (~200 lines)

```cpp
#pragma once

#include "commrat/module/module_core.hpp"
#include "commrat/module/module_mixins.hpp"
#include "commrat/module/module_services.hpp"
#include "commrat/module/traits/processor_bases.hpp"

namespace commrat {

/**
 * @brief Base for periodic single-output modules
 * 
 * Provides process(OutputData& output) virtual function.
 * Executes at fixed period (config.period).
 */
template<typename Registry, typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
class PeriodicSingleOutputBase
    // ONLY what we need for this mode:
    : public SingleOutputProcessorBase<void, typename ExtractOutputPayload<OutputSpec_>::type>
    , public LifecycleManager<PeriodicSingleOutputBase<Registry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public LoopExecutor<PeriodicSingleOutputBase<Registry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public MailboxInfrastructureBuilder<PeriodicSingleOutputBase<Registry, OutputSpec_, InputSpec_, CommandTypes...>, Registry>
    , public InputMetadataManager<PeriodicSingleOutputBase<Registry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public CommandDispatcher<PeriodicSingleOutputBase<Registry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>
    , public MultiOutputManager<PeriodicSingleOutputBase<Registry, OutputSpec_, InputSpec_, CommandTypes...>, Registry, typename OutputTypesTuple<OutputSpec_>::type>
    , public WorkLoopHandler<PeriodicSingleOutputBase<Registry, OutputSpec_, InputSpec_, CommandTypes...>>
{
    // Friend declarations for CRTP mixins
    friend class LifecycleManager<PeriodicSingleOutputBase>;
    friend class LoopExecutor<PeriodicSingleOutputBase>;
    friend class MailboxInfrastructureBuilder<PeriodicSingleOutputBase, Registry>;
    friend class InputMetadataManager<PeriodicSingleOutputBase>;
    friend class CommandDispatcher<PeriodicSingleOutputBase, CommandTypes...>;
    friend class MultiOutputManager<PeriodicSingleOutputBase, Registry, typename OutputTypesTuple<OutputSpec_>::type>;
    friend class WorkLoopHandler<PeriodicSingleOutputBase>;
    
private:
    using ModuleTypes = module_traits::ModuleTypes<Registry, OutputSpec_, InputSpec_, CommandTypes...>;
    
public:
    // Public type aliases
    using OutputData = typename ModuleTypes::OutputData;
    using InputData = typename ModuleTypes::InputData;  // void for periodic
    
    static constexpr bool has_periodic_input = true;
    static constexpr bool has_continuous_input = false;
    static constexpr bool has_loop_input = false;
    static constexpr bool has_multi_input = false;
    static constexpr bool has_multi_output = false;
    
protected:
    ModuleConfig config_;
    typename ModuleTypes::MailboxSetTuple mailbox_infrastructure_;
    typename ModuleTypes::SubscriptionProtocolType subscription_protocol_;
    typename ModuleTypes::PublisherType publisher_;
    std::atomic<bool> running_;
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    std::array<InputMetadataStorage, 1> input_metadata_;  // Size 1 (unused for periodic)
    
    // Constructor, lifecycle methods, etc.
    // ... (same as current Module, but specific to this mode)
};

} // namespace commrat
```

**Key Points**:
- **Only 8 base classes** (vs current 13+)
- **No** `ContinuousProcessorBase` - not needed!
- **No** `MultiOutputProcessorBase` - not needed!
- **No** `MultiInputInfrastructure` - not needed!
- **No** `MultiInputProcessor` - not needed!
- **No** conditional `std::conditional_t` - everything is unconditional!

#### Example 2: MultiInputMultiOutputBase

**File**: `include/commrat/module/base/multi_input_multi_base.hpp` (~300 lines)

```cpp
template<typename Registry, typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
class MultiInputMultiOutputBase
    // ONLY what we need for multi-input + multi-output:
    : public MultiOutputProcessorBase<typename OutputTypesTuple<OutputSpec_>::type, typename ExtractInputTypes<InputSpec_>::type>
    , public MultiInputInfrastructure<...>
    , public MultiInputProcessor<...>
    , public MultiOutputManager<...>
    , public LifecycleManager<...>
    , public LoopExecutor<...>
    , public CommandDispatcher<...>
    , public MailboxInfrastructureBuilder<...>
    , public InputMetadataManager<...>
    , public WorkLoopHandler<...>
{
    // NO SingleOutputProcessorBase - not needed!
    // NO ContinuousProcessorBase - not needed!
    
    static constexpr bool has_periodic_input = false;
    static constexpr bool has_continuous_input = false;
    static constexpr bool has_loop_input = false;
    static constexpr bool has_multi_input = true;
    static constexpr bool has_multi_output = true;
};
```

---

## Files Affected - Complete Analysis

### Core Module Files (3 files - Major Changes)

#### 1. `include/commrat/registry_module.hpp` 
**Current**: 575 lines, 13+ base inheritance, monolithic  
**Action**: RENAME to `registry_module_legacy.hpp`, mark deprecated  
**Reason**: Keep for reference during migration, delete after completion

#### 2. `include/commrat/module.hpp` (NEW)
**Lines**: ~100  
**Purpose**: User-facing API, thin wrapper over SelectModuleBase  
**Changes**: Clean documentation, no implementation details

#### 3. `include/commrat/module/base/module_base_selector.hpp` (NEW)
**Lines**: ~80  
**Purpose**: Compile-time dispatch to appropriate base variant  
**Changes**: Type traits for input/output mode detection

### Base Implementation Files (8 NEW files)

#### 4-11. Base Variant Implementations (NEW)

| File | Lines | Purpose | Key Bases Inherited |
|------|-------|---------|---------------------|
| `periodic_single_base.hpp` | ~200 | Periodic + single output | SingleOutputProcessorBase, LifecycleManager, LoopExecutor |
| `periodic_multi_base.hpp` | ~250 | Periodic + multi-output | MultiOutputProcessorBase, MultiOutputManager |
| `continuous_single_base.hpp` | ~220 | Input<T> + single output | ContinuousProcessorBase, LifecycleManager |
| `continuous_multi_base.hpp` | ~270 | Input<T> + multi-output | ContinuousProcessorBase, MultiOutputProcessorBase |
| `multi_input_single_base.hpp` | ~280 | Inputs<...> + single output | MultiInputProcessor, MultiInputInfrastructure |
| `multi_input_multi_base.hpp` | ~320 | Inputs<...> + multi-output | MultiInputProcessor, MultiOutputProcessorBase |
| `loop_single_base.hpp` | ~200 | LoopInput + single output | SingleOutputProcessorBase, LoopExecutor |
| `loop_multi_base.hpp` | ~250 | LoopInput + multi-output | MultiOutputProcessorBase, LoopExecutor |

**Total new code**: ~1990 lines (but eliminates workarounds and improves maintainability)

### Processor Base Files (3 files - Minor Changes)

#### 12. `include/commrat/module/traits/processor_bases.hpp`
**Current**: 156 lines with workaround specializations  
**Action**: REMOVE `<void, void>` specializations - no longer needed!  
**Changes**: Delete lines 24-29 (ContinuousProcessorBase specializations), lines 102-107 (SingleOutputProcessorBase<void,void>)  
**New size**: ~140 lines (-10%)

**Deletions**:
```cpp
// DELETE - no longer needed:
template<typename InputData_>
class ContinuousProcessorBase<InputData_, void> { };

template<>
class ContinuousProcessorBase<void, void> { };

template<>
class SingleOutputProcessorBase<void, void> { };
```

#### 13. `include/commrat/module/traits/multi_input_resolver.hpp`
**Current**: Uses `ResolveMultiInputBase<InputSpec, OutputSpec>::type`  
**Action**: Can simplify - each base knows if it needs multi-input support  
**Changes**: May be able to delete entire file - multi-input bases directly inherit

#### 14. `include/commrat/module/io/multi_output_manager.hpp`
**Current**: Conditionally used via inheritance  
**Action**: No changes needed - still used by multi-output bases  
**Status**: UNCHANGED

### Mixin Files (7 files - No Changes Required)

All CRTP mixins remain unchanged - they're just composed differently:

| File | Status | Reason |
|------|--------|--------|
| `module/lifecycle/lifecycle_manager.hpp` | UNCHANGED | Just changes template parameter type |
| `module/lifecycle/loop_executor.hpp` | UNCHANGED | Works with any base type |
| `module/services/publishing.hpp` | UNCHANGED | Template-agnostic |
| `module/services/subscription.hpp` | UNCHANGED | Template-agnostic |
| `module/metadata/input_metadata.hpp` | UNCHANGED | Template-agnostic |
| `module/io/multi_input_infrastructure.hpp` | UNCHANGED | Only used by multi-input bases |
| `module/io/multi_input_processor.hpp` | UNCHANGED | Only used by multi-input bases |

**Total**: 7 files, 0 changes needed

### Type Trait Files (3 files - Minor Updates)

#### 15. `include/commrat/module/traits/module_types.hpp`
**Current**: Computes all type traits for any mode  
**Action**: SIMPLIFY - can remove some conditionals  
**Changes**: Each base can import only the traits it needs  
**Example**: PeriodicSingleOutputBase doesn't need `DataTypesTuple` (no continuous input)

#### 16. `include/commrat/module/traits/type_extraction.hpp`
**Current**: Clean, just consolidated  
**Action**: NO CHANGES  
**Status**: UNCHANGED

#### 17. `include/commrat/module/traits/io_traits.hpp`
**Current**: Input/output mode detection  
**Action**: Some helpers moved to module_base_selector.hpp  
**Changes**: Extract `is_periodic_input_v`, `is_multi_input_v` etc. to selector

### Configuration and Support Files (2 files - No Changes)

| File | Status |
|------|--------|
| `include/commrat/module/config/module_config.hpp` | UNCHANGED |
| `include/commrat/module/mailbox/mailbox_set.hpp` | UNCHANGED |

### Public API Files (1 file - Minor Update)

#### 18. `include/commrat/commrat.hpp`
**Current**: Includes `registry_module.hpp`  
**Action**: Change include to new `module.hpp`  
**Changes**: 1 line:
```cpp
// OLD:
#include "commrat/registry_module.hpp"

// NEW:
#include "commrat/module.hpp"
```

### Test Files (0 changes - API compatible!)

All example and test files remain unchanged - the user-facing API is identical!

**Files**: 22 test/example files  
**Changes**: 0 lines  
**Reason**: `Module<Registry, Output<T>, PeriodicInput>` still works exactly the same

---

## Impact Summary

### Files Changed

| Category | Files | New | Modified | Deleted | Total Lines |
|----------|-------|-----|----------|---------|-------------|
| Core Module | 3 | 2 | 0 | 1 | +180, -575 |
| Base Implementations | 8 | 8 | 0 | 0 | +1990 |
| Processor Bases | 3 | 0 | 1 | 2 | -16 |
| Type Traits | 3 | 0 | 1 | 0 | ~0 |
| Public API | 1 | 0 | 1 | 0 | 1 line |
| Mixins | 7 | 0 | 0 | 0 | 0 |
| Tests/Examples | 22 | 0 | 0 | 0 | 0 |
| **TOTAL** | **47** | **10** | **3** | **3** | **+1579 net** |

### Code Metrics

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Module API lines | 575 | 100 | -82% |
| Base inheritance per mode | 13+ | 8-10 | -30% |
| Workaround specializations | 3 | 0 | -100% |
| Template instantiations | High | Medium | -30% est. |
| Compilation time | Baseline | -20% est. | Faster |
| Error message length | 500+ lines | 200 lines | -60% |

---

## Migration Strategy

### Phase 1: Proof of Concept (1-2 days)
1. Create `periodic_single_base.hpp` (simplest case)
2. Create minimal `module_base_selector.hpp`
3. Create new `module.hpp` with only periodic single-output support
4. Verify one example builds and runs: `examples/clean_interface_example.cpp`

**Validation**: If this works, architecture is sound

### Phase 2: Implement All 8 Bases (3-4 days)
1. Implement remaining 7 base variants
2. Complete `module_base_selector.hpp` with all 8 modes
3. Update `module.hpp` documentation for all modes

**Validation**: All examples compile (may need minor fixes)

### Phase 3: Cleanup (1 day)
1. Remove workaround specializations from `processor_bases.hpp`
2. Delete `EmptyBase2`, `EmptyBase3` dummy classes
3. Simplify `module_types.hpp` (remove unused trait computations)
4. Update `commrat.hpp` include

**Validation**: All tests pass

### Phase 4: Testing and Documentation (1-2 days)
1. Run full test suite (22 tests)
2. Update USER_GUIDE.md with new architecture
3. Add migration notes to README.md
4. Archive old `registry_module.hpp` to docs/archive/

**Validation**: All tests pass, documentation complete

### Total Effort: 6-9 days

---

## Risk Analysis

### High Risk Items

1. **CRTP mixin compatibility**: Mixins expect specific method signatures
   - **Mitigation**: Each base provides same interface as current Module
   - **Validation**: Compile each base independently

2. **Friend declaration changes**: Mixins may need friend access
   - **Mitigation**: Copy friend declarations to each base
   - **Validation**: Compiler will catch missing friends

3. **Constructor complexity**: Each base needs proper initialization
   - **Mitigation**: Extract constructor logic to helper methods
   - **Validation**: Run all lifecycle tests

### Medium Risk Items

1. **Type trait changes**: Some traits may need updates
   - **Mitigation**: Keep `ModuleTypes` unchanged initially
   - **Validation**: Static assertions catch incompatibilities

2. **Template instantiation order**: Bases may instantiate in different order
   - **Mitigation**: Don't rely on instantiation order
   - **Validation**: All compiler warnings addressed

### Low Risk Items

1. **Documentation drift**: New structure needs updated docs
   - **Mitigation**: Update docs as part of implementation
   - **Validation**: User guide review

2. **Example updates**: Examples may need minor tweaks
   - **Mitigation**: User API unchanged - should work as-is
   - **Validation**: All examples compile and run

---

## Benefits Analysis

### Immediate Benefits

1. **Eliminate workarounds**: No more `<void, void>` specializations
2. **Clearer errors**: Only relevant bases instantiated in error backtraces
3. **Faster iteration**: Changing one mode doesn't affect others
4. **Better documentation**: User sees clean API, not implementation

### Long-Term Benefits

1. **Easier to add new modes**: Create new base, add to selector
2. **Better testing**: Can test each base independently
3. **Reduced coupling**: Each base is self-contained
4. **Maintainability**: Changes isolated to specific base files

### Performance Benefits

1. **Compilation time**: Fewer template instantiations per translation unit
2. **Binary size**: Dead code elimination more effective (no unused bases)
3. **Runtime**: No change - same generated code

---

## Alternative Approaches Considered

### Alternative 1: Keep Monolithic, Fix with More SFINAE
**Approach**: Add more template constraints to existing bases  
**Rejected**: Makes code even more complex, doesn't solve root problem

### Alternative 2: Full CRTP for Processor Bases
**Approach**: Make processor bases CRTP mixins instead of inheritance  
**Rejected**: Requires virtual function dispatch somewhere, loses type safety

### Alternative 3: Policy-Based Design
**Approach**: Use template policies for each aspect  
**Rejected**: Harder to reason about, less familiar to users

### Alternative 4: Type Erasure with std::function
**Approach**: Use type erasure for process() functions  
**Rejected**: Loses compile-time type safety, real-time concerns

---

## Recommendation

**PROCEED** with proposed architecture.

**Rationale**:
1. Solves real problems (workarounds, error messages, maintainability)
2. Low risk (user API unchanged, systematic migration)
3. High reward (cleaner code, faster compilation, easier maintenance)
4. Proven pattern (used in Boost.Asio, Abseil, other large C++ frameworks)

**Next Steps**:
1. Get approval for architectural direction
2. Implement Phase 1 proof of concept
3. Review proof of concept with team
4. Proceed with full implementation if approved

---

## Open Questions

1. **Naming**: Should bases be in `commrat::module::base` or `commrat::detail`?
   - **Recommendation**: `commrat::module::base` - not truly "detail" since bases are documented

2. **Legacy support**: Keep old `registry_module.hpp` or delete immediately?
   - **Recommendation**: Keep as `registry_module_legacy.hpp` for one release cycle

3. **Documentation level**: Document all 8 bases or just the selector?
   - **Recommendation**: Document selector + 2 example bases, rest are implementation

4. **Testing strategy**: Unit test each base or integration test only?
   - **Recommendation**: Both - unit test base construction, integration test process() calls

---

## Appendix: Code Size Comparison

### Current (Monolithic)
- `registry_module.hpp`: 575 lines
- `processor_bases.hpp`: 156 lines (with workarounds)
- **Total user-facing**: 731 lines

### Proposed (Layered)
- `module.hpp`: 100 lines (user-facing)
- `module_base_selector.hpp`: 80 lines (hidden)
- 8 base implementations: 1990 lines (hidden)
- `processor_bases.hpp`: 140 lines (simplified)
- **Total user-facing**: 100 lines (85% reduction!)
- **Total implementation**: 2210 lines (+200% but organized)

### Net Result
- User sees 85% less code
- Implementation is 200% larger BUT:
  - Isolated by mode (easier to maintain)
  - No workarounds needed
  - Clearer purpose per file
  - Dead code elimination removes unused bases

---

**END OF PROPOSAL**
