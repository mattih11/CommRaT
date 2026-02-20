#pragma once

#include <tuple>
#include <type_traits>
#include <cstddef>

namespace commrat {

// Forward declarations
template<typename CommratApp, typename T> class CmdInput;
template<typename CommratApp, typename T> class ContinuousInput;
template<typename CommratApp, typename T> class SyncedInput;
template<typename CommratApp, typename T, std::size_t SLOTS> class BufferedOutput;

// ============================================================================
// Input Type Traits
// ============================================================================

/**
 * @brief Check if type is ContinuousInput
 */
template<typename T>
struct is_continuous_input : std::false_type {};

template<typename CommratApp, typename T>
struct is_continuous_input<ContinuousInput<CommratApp, T>> : std::true_type {};

template<typename T>
inline constexpr bool is_continuous_input_v = is_continuous_input<T>::value;

/**
 * @brief Check if type is SyncedInput
 */
template<typename T>
struct is_synced_input : std::false_type {};

template<typename CommratApp, typename T>
struct is_synced_input<SyncedInput<CommratApp, T>> : std::true_type {};

template<typename T>
inline constexpr bool is_synced_input_v = is_synced_input<T>::value;

/**
 * @brief Check if type is CmdInput (base class)
 */
template<typename T>
struct is_cmd_input : std::false_type {};

template<typename CommratApp, typename T>
struct is_cmd_input<CmdInput<CommratApp, T>> : std::true_type {};

template<typename T>
inline constexpr bool is_cmd_input_v = is_cmd_input<T>::value;

/**
 * @brief Find index of ContinuousInput in tuple
 * 
 * Searches tuple for the first (and should be only) ContinuousInput type.
 * Returns index or tuple_size if not found.
 */
template<typename Tuple, std::size_t I = 0>
struct find_continuous_input_index {
    static constexpr std::size_t value = 
        (I < std::tuple_size_v<Tuple>) ?
            (is_continuous_input_v<std::tuple_element_t<I, Tuple>> ? I : 
             find_continuous_input_index<Tuple, I + 1>::value) :
            std::tuple_size_v<Tuple>;
};

template<typename Tuple>
inline constexpr std::size_t find_continuous_input_index_v = find_continuous_input_index<Tuple>::value;

/**
 * @brief Count ContinuousInput types in tuple
 */
template<typename Tuple, std::size_t I = 0>
struct count_continuous_inputs {
    static constexpr std::size_t value = 
        (I < std::tuple_size_v<Tuple>) ?
            (is_continuous_input_v<std::tuple_element_t<I, Tuple>> ? 1 : 0) +
            count_continuous_inputs<Tuple, I + 1>::value :
            0;
};

template<typename Tuple>
inline constexpr std::size_t count_continuous_inputs_v = count_continuous_inputs<Tuple>::value;

/**
 * @brief Check if tuple has exactly one ContinuousInput at compile time
 */
template<typename Tuple>
consteval bool has_single_continuous_input() {
    return count_continuous_inputs_v<Tuple> == 1;
}

/**
 * @brief Check if tuple has zero continuous inputs at compile time
 */
template<typename Tuple>
consteval bool has_no_continuous_input() {
    return count_continuous_inputs_v<Tuple> == 0;
}

/**
 * @brief Check if tuple has at most one ContinuousInput (0 or 1)
 */
template<typename Tuple>
consteval bool has_at_most_one_continuous_input() {
    return count_continuous_inputs_v<Tuple> <= 1;
}

/**
 * @brief Concept: Input tuple must have at most one ContinuousInput
 * 
 * Execution modes:
 * - 1 ContinuousInput: Input-driven execution
 * - 0 ContinuousInput: Periodic (with period) or free-running (no period)
 */
template<typename Tuple>
concept ValidInputTuple = requires {
    requires count_continuous_inputs_v<Tuple> <= 1;
};

/**
 * @brief Get the ContinuousInput type from tuple
 * 
 * Extracts the type of the ContinuousInput in the tuple.
 * Only valid when exactly one ContinuousInput exists.
 */
template<typename Tuple>
    requires ValidInputTuple<Tuple> && (count_continuous_inputs_v<Tuple> == 1)
struct get_continuous_input_type {
    static constexpr std::size_t index = find_continuous_input_index_v<Tuple>;
    
    using type = std::tuple_element_t<index, Tuple>;
};

template<typename Tuple>
using get_continuous_input_type_t = typename get_continuous_input_type<Tuple>::type;

// ============================================================================
// Output Type Traits
// ============================================================================

/**
 * @brief Check if type is BufferedOutput
 */
template<typename T>
struct is_buffered_output : std::false_type {};

template<typename CommratApp, typename T, std::size_t SLOTS>
struct is_buffered_output<BufferedOutput<CommratApp, T, SLOTS>> : std::true_type {};

template<typename T>
inline constexpr bool is_buffered_output_v = is_buffered_output<T>::value;

/**
 * @brief Extract payload type from input
 */
template<typename T>
struct input_payload_type;

template<typename CommratApp, typename T>
struct input_payload_type<ContinuousInput<CommratApp, T>> {
    using type = T;
};

template<typename CommratApp, typename T>
struct input_payload_type<SyncedInput<CommratApp, T>> {
    using type = T;
};

template<typename CommratApp, typename T>
struct input_payload_type<CmdInput<CommratApp, T>> {
    using type = T;
};

template<typename T>
using input_payload_type_t = typename input_payload_type<T>::type;

/**
 * @brief Extract payload type from output
 */
template<typename T>
struct output_payload_type;

template<typename CommratApp, typename T, std::size_t SLOTS>
struct output_payload_type<BufferedOutput<CommratApp, T, SLOTS>> {
    using type = T;
};

template<typename T>
using output_payload_type_t = typename output_payload_type<T>::type;

} // namespace commrat
