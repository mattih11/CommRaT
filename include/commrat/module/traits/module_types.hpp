#pragma once

#include "commrat/module/io_spec.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/mailbox/mailbox_set.hpp"
#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include <tuple>
#include <type_traits>

namespace commrat {
namespace module_traits {

// ============================================================================
// Phase 1: Type Computation Helpers (Extracted from registry_module.hpp)
// ============================================================================

/**
 * @brief Helper to check if there's an explicit PrimaryInput in CommandTypes
 * 
 * PrimaryInput is passed as a command type to Module<..., Inputs<>, PrimaryInput<T>>
 */
template<typename...>
struct HasPrimaryInputHelper : std::false_type {};

template<typename T, typename... Rest>
struct HasPrimaryInputHelper<PrimaryInput<T>, Rest...> : std::true_type {};

template<typename First, typename... Rest>
struct HasPrimaryInputHelper<First, Rest...> : HasPrimaryInputHelper<Rest...> {};

/**
 * @brief Helper to extract primary payload type from CommandTypes
 */
template<typename...>
struct ExtractPrimaryPayloadHelper {
    using type = void;
};

template<typename T, typename... Rest>
struct ExtractPrimaryPayloadHelper<PrimaryInput<T>, Rest...> {
    using type = T;
};

template<typename First, typename... Rest>
struct ExtractPrimaryPayloadHelper<First, Rest...> : ExtractPrimaryPayloadHelper<Rest...> {};

/**
 * @brief Generate MailboxSet tuple for each output type (Phase 7.4)
 */
template<typename UserRegistry, typename OutputTypesTuple, typename... CommandTypes>
struct MakeMailboxSetTuple;

template<typename UserRegistry, typename... OutputTypes, typename... CommandTypes>
struct MakeMailboxSetTuple<UserRegistry, std::tuple<OutputTypes...>, CommandTypes...> {
    using type = std::tuple<MailboxSet<UserRegistry, OutputTypes, CommandTypes...>...>;
};

/**
 * @brief Create typed CMD mailbox (Commands + Outputs)
 */
template<typename UserRegistry, typename... Ts>
struct MakeTypedCmdMailbox;

template<typename UserRegistry, typename... Ts>
struct MakeTypedCmdMailbox<UserRegistry, std::tuple<Ts...>> {
    using type = std::conditional_t<
        sizeof...(Ts) == 0,
        RegistryMailbox<UserRegistry>,  // No types → regular mailbox
        TypedMailbox<UserRegistry, Ts...>  // Has types → restrict
    >;
};

/**
 * @brief Extract data types from InputSpec for DATA mailbox
 */
template<typename T>
struct ExtractDataTypes { 
    using type = std::tuple<>; 
};

template<typename T>
struct ExtractDataTypes<Input<T>> { 
    using type = std::tuple<T>; 
};

template<typename... Ts>
struct ExtractDataTypes<Inputs<Ts...>> { 
    using type = std::tuple<Ts...>; 
};

/**
 * @brief Create typed DATA mailbox (Input data types only)
 */
template<typename UserRegistry, typename Tuple>
struct MakeTypedDataMailbox;

template<typename UserRegistry, typename... Ts>
struct MakeTypedDataMailbox<UserRegistry, std::tuple<Ts...>> {
    using type = std::conditional_t<
        sizeof...(Ts) == 0,
        RegistryMailbox<UserRegistry>,  // No inputs → regular mailbox
        TypedMailbox<UserRegistry, Ts...>  // Has inputs → restrict to input types
    >;
};

// ============================================================================
// ModuleTypes: Central Type Computation
// ============================================================================

/**
 * @brief Centralized type computation for Module class
 * 
 * This struct computes all derived types needed by Module at compile time,
 * extracting the complex metaprogramming logic from the Module class itself.
 * 
 * @tparam UserRegistry The application's MessageRegistry
 * @tparam OutputSpec_ User-provided output specification (normalized internally)
 * @tparam InputSpec_ User-provided input specification (normalized internally)
 * @tparam CommandTypes Optional command types this module handles
 */
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
struct ModuleTypes {
    // Normalize specs: raw type T -> Output<T>, ContinuousInput<T> -> Input<T>
    using OutputSpec = NormalizeOutput_t<OutputSpec_>;
    using InputSpec = NormalizeInput_t<InputSpec_>;
    
    // Input type analysis
    using InputTypesTuple = typename ExtractInputTypes<InputSpec>::type;
    static constexpr size_t InputCount = std::tuple_size_v<InputTypesTuple>;
    static constexpr bool has_multi_input = InputCount > 1;
    
    // Primary input handling
    static constexpr bool has_primary_input_spec = HasPrimaryInputHelper<CommandTypes...>::value;
    using PrimaryPayloadType = typename ExtractPrimaryPayloadHelper<CommandTypes...>::type;
    
    // Output type analysis
    using OutputTypesTuple = typename ::commrat::OutputTypesTuple<OutputSpec>::type;
    static constexpr size_t num_output_types = std::tuple_size_v<OutputTypesTuple>;
    static constexpr bool has_multi_output = OutputCount_v<OutputSpec> > 1;
    
    // Mailbox structure selection
    static constexpr bool use_mailbox_sets = (num_output_types > 1);
    
    // Generate MailboxSet tuple for multi-output
    using MailboxSetTuple = typename MakeMailboxSetTuple<UserRegistry, OutputTypesTuple, CommandTypes...>::type;
    
    // Mailbox types
    using CommandTuple = std::tuple<CommandTypes...>;
    using CombinedCmdTypes = decltype(std::tuple_cat(
        std::declval<CommandTuple>(),
        std::declval<OutputTypesTuple>()
    ));
    
    using CmdMailbox = typename MakeTypedCmdMailbox<UserRegistry, CommandTuple>::type;
    using WorkMailbox = RegistryMailbox<SystemRegistry>;
    using PublishMailbox = typename MakeTypedCmdMailbox<UserRegistry, OutputTypesTuple>::type;
    
    using DataTypesTuple = typename ExtractDataTypes<InputSpec>::type;
    using DataMailbox = typename MakeTypedDataMailbox<UserRegistry, DataTypesTuple>::type;
    
    // Public type aliases (user-visible)
    using OutputData = typename ExtractOutputPayload<OutputSpec>::type;
    using InputData = typename ExtractInputPayload<InputSpec>::type;
    
    // Input mode flags
    static constexpr bool has_continuous_input = HasContinuousInput<InputSpec>;
    static constexpr bool has_periodic_input = std::is_same_v<InputSpec, PeriodicInput>;
    static constexpr bool has_loop_input = std::is_same_v<InputSpec, LoopInput>;
};

}  // namespace module_traits
}  // namespace commrat
