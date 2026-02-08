#pragma once

#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include <cstddef>

namespace commrat {

// ============================================================================
// Multi-Input Base Resolution
// ============================================================================

// Helper to resolve MultiInputProcessorBase template arguments
// (workaround for complex nested template parsing issues)
template<typename InputSpec_, typename OutputSpec_>
struct ResolveMultiInputBase {
    using NormalizedInput = typename NormalizeInput<InputSpec_>::Type;
    using InputTypesTuple = typename ExtractInputTypes<NormalizedInput>::type;
    using NormalizedOutput = typename NormalizeOutput<OutputSpec_>::Type;
    using OutputData = typename ExtractOutputPayload<NormalizedOutput>::type;
    static constexpr std::size_t InputCount = std::tuple_size_v<InputTypesTuple>;
    
    using type = MultiInputProcessorBase<InputTypesTuple, OutputData, InputCount>;
};

// Specialization for multi-output case (Outputs<Ts...>)
template<typename InputSpec_, typename... OutputTypes>
struct ResolveMultiInputBase<InputSpec_, Outputs<OutputTypes...>> {
    using NormalizedInput = typename NormalizeInput<InputSpec_>::Type;
    using InputTypesTuple = typename ExtractInputTypes<NormalizedInput>::type;
    using OutputData = std::tuple<OutputTypes...>;  // Multi-output uses tuple
    static constexpr std::size_t InputCount = std::tuple_size_v<InputTypesTuple>;
    
    using type = MultiInputProcessorBase<InputTypesTuple, OutputData, InputCount>;
};

} // namespace commrat
