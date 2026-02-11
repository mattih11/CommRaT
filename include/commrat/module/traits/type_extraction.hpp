#pragma once

#include "commrat/module/io_spec.hpp"
#include "commrat/module/module_config.hpp"
#include <tuple>
#include <type_traits>

namespace commrat {

// ============================================================================
// Type Extraction Helpers
// ============================================================================

// Helper to extract InputData from InputSpec (outside Module class)
template<typename T>
struct ExtractInputPayload {
    using type = void;  // PeriodicInput, LoopInput
};

template<typename T>
struct ExtractInputPayload<Input<T>> {
    using type = T;
};

// Helper to extract input types tuple from InputSpec (outside Module)
template<typename T>
struct ExtractInputTypes {
    using type = std::tuple<>;  // Default: no inputs (PeriodicInput, LoopInput)
};

template<typename T>
struct ExtractInputTypes<Input<T>> {
    using type = std::tuple<T>;  // Single input
};

template<typename... Ts>
struct ExtractInputTypes<Inputs<Ts...>> {
    using type = std::tuple<Ts...>;  // Multi-input
};

// Helper to extract OutputData from OutputSpec (outside Module class)
template<typename T>
struct ExtractOutputPayload {
    using type = T;  // Raw type
};

template<typename T>
struct ExtractOutputPayload<Output<T>> {
    using type = T;
};

template<typename... Ts>
struct ExtractOutputPayload<Outputs<Ts...>> {
    using type = void;  // Multi-output: void process(T1& out1, T2& out2, ...)
};

// Helper to get output types as tuple (outside Module class)
template<typename T>
struct OutputTypesTuple {
    using type = std::tuple<T>;  // Raw type
};

template<typename T>
struct OutputTypesTuple<Output<T>> {
    using type = std::tuple<T>;
};

template<typename... Ts>
struct OutputTypesTuple<Outputs<Ts...>> {
    using type = std::tuple<Ts...>;
};

} // namespace commrat
