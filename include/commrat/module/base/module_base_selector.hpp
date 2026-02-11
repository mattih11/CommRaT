#pragma once

#include "commrat/module/base/noinput_single_base.hpp"
#include "commrat/module/base/singleinput_single_base.hpp"
#include "commrat/module/base/noinput_multi_base.hpp"
#include "commrat/module/base/singleinput_multi_base.hpp"
#include "commrat/module/io_spec.hpp"

namespace commrat {

/**
 * @brief Select appropriate ModuleBase variant based on I/O specs
 * 
 * Compile-time selection of specialized base implementations:
 * - PeriodicInput/LoopInput + single output → NoInputSingleOutputBase
 * - Input<T> + single output               → SingleInputSingleOutputBase
 * - PeriodicInput/LoopInput + multi-output → NoInputMultiOutputBase
 * - Input<T> + multi-output                → SingleInputMultiOutputBase
 * - Inputs<T,U,V> + single output          → MultiInputSingleOutputBase (TODO)
 * - Inputs<T,U,V> + multi-output           → MultiInputMultiOutputBase (TODO)
 * 
 * Phase 7.1: 4 of 6 bases implemented
 */
template<typename Registry, typename OutputSpec, typename InputSpec, typename... CommandTypes>
using SelectModuleBase = 
    typename std::conditional_t<
        // No-input + single output
        (is_periodic_input_v<InputSpec> || is_loop_input_v<InputSpec>) && is_single_output_v<OutputSpec>,
        NoInputSingleOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
        typename std::conditional_t<
            // Single-input + single output
            HasContinuousInput<InputSpec> && is_single_output_v<OutputSpec>,
            SingleInputSingleOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
            typename std::conditional_t<
                // No-input + multi-output
                (is_periodic_input_v<InputSpec> || is_loop_input_v<InputSpec>) && is_multi_output_v<OutputSpec>,
                NoInputMultiOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
                typename std::conditional_t<
                    // Single-input + multi-output
                    HasContinuousInput<InputSpec> && is_multi_output_v<OutputSpec>,
                    SingleInputMultiOutputBase<Registry, OutputSpec, InputSpec, CommandTypes...>,
                    // TODO: Multi-input bases
                    void
                >
            >
        >
    >;

// Compile-time validation that we selected a valid base
template<typename Base>
struct ValidateBaseSelection {
    static_assert(!std::is_void_v<Base>, 
        "Unsupported I/O combination. Currently 4 of 6 bases implemented: "
        "NoInput/SingleInput × Single/Multi output. Multi-input coming soon.");
};

} // namespace commrat
