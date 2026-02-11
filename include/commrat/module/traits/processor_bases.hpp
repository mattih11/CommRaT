#pragma once

#include "commrat/module/io_spec.hpp"
#include <iostream>
#include <tuple>

namespace commrat {

// ============================================================================
// Helper Base Classes for Conditional Virtual Functions
// ============================================================================

// Base class providing process when InputData is not void
template<typename InputData_, typename OutputData_>
class ContinuousProcessorBase {
protected:
    virtual void process(const InputData_& input, OutputData_& output) {
        std::cerr << "[Module] ERROR: process(const Input&, Output&) not overridden in derived class!\n";
        (void)input;  // Suppress unused warning
        output = OutputData_{};
    }
};

// Specialization for void InputData (no process function)
template<typename OutputData_>
class ContinuousProcessorBase<void, OutputData_> {
    // No process for periodic/loop modes
};

// Specialization for void OutputData (multi-output)
template<typename InputData_>
class ContinuousProcessorBase<InputData_, void> {
    // No process for multi-output modules (use MultiOutputProcessorBase instead)
};

// Full specialization for both void
template<>
class ContinuousProcessorBase<void, void> {
    // No process for multi-output periodic/loop modules
};

// ============================================================================
// Multi-Output Processor Base (Phase 5.3)
// ============================================================================

// Helper to generate multi-output process signature
template<typename OutputTypesTuple, typename InputData_>
class MultiOutputProcessorBase;

// Single output: OutputData process() or OutputData process(const InputData&)
template<typename T>
class MultiOutputProcessorBase<std::tuple<T>, void> {
    // Single output handled by normal process() - no additional signature
};

template<typename T, typename InputData_>
class MultiOutputProcessorBase<std::tuple<T>, InputData_> {
    // Single output with continuous input - handled by process()
};

// Multi-output without continuous input: void process(T1& out1, T2& out2, ...)
template<typename... Ts>
    requires (sizeof...(Ts) > 1)
class MultiOutputProcessorBase<std::tuple<Ts...>, void> {
public:
    // Public virtual function for polymorphic calls from Module
    virtual void process(Ts&... outputs) {
        std::cerr << "[Module] ERROR: Multi-output process(...) not overridden in derived class!\n";
        // Leave outputs as default-constructed
    }
};

// Multi-output with continuous input: void process(const InputData&, T1& out1, T2& out2, ...)
template<typename... Ts, typename InputData_>
    requires (sizeof...(Ts) > 1)
class MultiOutputProcessorBase<std::tuple<Ts...>, InputData_> {
public:
    // Public virtual function for polymorphic calls from Module
    virtual void process(const InputData_& input, Ts&... outputs) {
        std::cerr << "[Module] ERROR: Multi-output process(...) not overridden in derived class!\n";
        // Leave outputs as default-constructed
        (void)input;  // Suppress unused warning
    }
};

// ============================================================================
// Single-Output Processor Base (conditional process() function)
// ============================================================================

// Helper base class that provides virtual process() only for single-output modules WITHOUT input
// Takes InputData_ to check if it's void (PeriodicInput/LoopInput)
template<typename InputData_, typename OutputData_>
class SingleOutputProcessorBase {
    // Empty - when InputData is not void, use ContinuousProcessorBase instead
};

// Specialization for void InputData (PeriodicInput/LoopInput): provides process(output&)
template<typename OutputData_>
class SingleOutputProcessorBase<void, OutputData_> {
protected:
    // Single output with no input: provide virtual process(output&)
    virtual void process(OutputData_& output) {
        std::cerr << "[Module] ERROR: process(Output&) not overridden in derived class!\\n";
        output = OutputData_{};
    }
};

// Specialization for void OutputData (multi-output): no process() function
template<typename InputData_>
class SingleOutputProcessorBase<InputData_, void> {
    // Empty - multi-output modules use MultiOutputProcessorBase::process(Ts&...) instead
};

// Full specialization for both void (multi-output with PeriodicInput/LoopInput)
template<>
class SingleOutputProcessorBase<void, void> {
    // Empty - multi-output modules use MultiOutputProcessorBase::process(Ts&...) instead
};

// ============================================================================
// Phase 6.7: Multi-Input Processor Base
// ============================================================================

// Helper base class that provides virtual multi-input process signatures
// Only enabled when InputCount > 1 (multi-input modules)

template<typename InputTypesTuple_, typename OutputData_, size_t InputCount_>
class MultiInputProcessorBase {
    // Primary template - should not be instantiated
    // Use specializations for InputCount == 0, 1, or > 1
};

// No multi-input (single input or no input): empty base
template<typename InputTypesTuple_, typename OutputData_>
class MultiInputProcessorBase<InputTypesTuple_, OutputData_, 0> {
    // Empty - not multi-input
};

template<typename InputTypesTuple_, typename OutputData_>
class MultiInputProcessorBase<InputTypesTuple_, OutputData_, 1> {
    // Empty - single input, not multi-input
};

// Multi-input with single output: void process(const T1&, const T2&, ..., OutputData&)
template<typename... Ts, typename OutputData_>
    requires (sizeof...(Ts) > 1 && !std::is_void_v<OutputData_>)
class MultiInputProcessorBase<std::tuple<Ts...>, OutputData_, sizeof...(Ts)> {
public:
    virtual void process(const Ts&... inputs, OutputData_& output) {
        std::cerr << "[Module] ERROR: Multi-input process(..., Output&) not overridden in derived class!\n";
        (void)std::make_tuple(inputs...);  // Suppress unused warnings
        output = OutputData_{};
    }
};

// Multi-input with multi-output: void process(const T1&, const T2&, ..., O1& out1, O2& out2, ...)
template<typename... InputTs, typename... OutputTs>
    requires (sizeof...(InputTs) > 1 && sizeof...(OutputTs) > 1)
class MultiInputProcessorBase<std::tuple<InputTs...>, std::tuple<OutputTs...>, sizeof...(InputTs)> {
public:
    virtual void process(const InputTs&... inputs, OutputTs&... outputs) {
        std::cerr << "[Module] ERROR: Multi-input+multi-output process(...) not overridden!\n";
        (void)std::make_tuple(inputs...);  // Suppress unused warnings
    }
};

} // namespace commrat
