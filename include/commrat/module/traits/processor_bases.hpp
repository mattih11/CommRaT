#pragma once

#include <tuple>
#include <type_traits>
#include "commrat/module/io/synced.hpp"

namespace commrat {

// Forward declarations for input types
template<typename Registry, typename T> class ContinuousInput;
template<typename Registry, typename T> class SyncedInputImpl;

// ============================================================================
// Helper: Map input type to process parameter type
// ============================================================================

/**
 * @brief Maps input wrapper type to the type passed to process()
 * 
 * - ContinuousInput<Registry, T> -> const T& (data always available)
 * - SyncedInput<Registry, T> -> Synced<T> (zero-copy wrapper with validity)
 * - T (bare type) -> const T& (direct use)
 */
template<typename InputWrapper>
struct ProcessParam {
    using type = InputWrapper;  // Default: use as-is
};

// Specialization for ContinuousInput - unwrap to payload type
template<typename Registry, typename T>
struct ProcessParam<ContinuousInput<Registry, T>> {
    using type = T;
};

// Specialization for SyncedInputImpl - wrap in Synced<T> (zero-copy)
template<typename Registry, typename T>
struct ProcessParam<SyncedInputImpl<Registry, T>> {
    using type = Synced<T>;
};

template<typename T>
using ProcessParam_t = typename ProcessParam<T>::type;

// ============================================================================
// Processor Base Classes (Module2 - Zero-Copy Process Signatures)
// ============================================================================

/**
 * @brief Base class for modules with no inputs
 * 
 * Provides: void process(T1& out1, T2& out2, ...)
 * 
 * Works for single output (tuple size 1) and multi-output (tuple size > 1)
 */
template<typename... OutputTypes>
class NoInputProcessor;

template<typename... Ts>
class NoInputProcessor<std::tuple<Ts...>> {
protected:
    virtual void process(Ts&... outputs) {
        // User must override
        ((outputs = Ts{}), ...);
    }
};

/**
 * @brief Base class for modules with inputs (continuous or synced)
 * 
 * Provides: void process(const ProcessParam_t<I1>&..., O1& out1, ...)
 * 
 * Input parameters automatically mapped:
 * - ContinuousInput<Registry, T> -> const T& (unwrapped)
 * - SyncedInput<Registry, T> -> Synced<T> (zero-copy wrapper)
 * 
 * Works for any combination of input/output counts.
 */
template<typename InputTuple, typename OutputTuple>
class InputProcessor;

template<typename... InputWrappers, typename... OutputTypes>
class InputProcessor<std::tuple<InputWrappers...>, std::tuple<OutputTypes...>> {
protected:
    // Inputs first (mapped via ProcessParam), then outputs (mutable refs)
    virtual void process(const ProcessParam_t<InputWrappers>&... inputs, OutputTypes&... outputs) {
        // User must override
        (void)std::make_tuple(inputs...);
        ((outputs = OutputTypes{}), ...);
    }
};

// ============================================================================
// Helper: Extract types from tuples and apply to processor base
// ============================================================================

template<typename InputTuple, typename OutputTuple>
struct NoInputBase {
    using type = NoInputProcessor<OutputTuple>;
};

template<typename InputTuple, typename OutputTuple>
struct InputBase {
    using type = InputProcessor<InputTuple, OutputTuple>;
};

} // namespace commrat
