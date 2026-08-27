#pragma once

#include <commrat/meta/descriptor.hpp>
#include <commrat/messaging/data_with_commands.hpp>
#include <commrat/module/helpers/command_extraction.hpp>
#include <commrat/module/io/io_spec.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>

#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace commrat {

namespace detail {

// Strip DataWithCommands wrapper to get the plain payload type name.
template<typename T>
std::string inspect_type_name() {
    if constexpr (is_data_with_commands<T>::value) {
        return rfl::type_name_t<typename T::Payload>().str();
    } else {
        return rfl::type_name_t<T>().str();
    }
}

template<typename OutputTypes, std::size_t... Is>
std::vector<std::string> output_names_impl(std::index_sequence<Is...>) {
    std::vector<std::string> names;
    names.reserve(sizeof...(Is));
    (names.push_back(inspect_type_name<std::tuple_element_t<Is, OutputTypes>>()), ...);
    return names;
}

template<typename OutputTypes>
std::vector<std::string> output_names() {
    return output_names_impl<OutputTypes>(
        std::make_index_sequence<std::tuple_size_v<OutputTypes>>{});
}

// Split input wrappers+types into continuous and synced name lists.
// InputWrappers carries ContinuousInput/SyncedInputImpl; InputTypes carries the payloads.
template<typename InputWrappers, typename InputTypes, std::size_t... Is>
std::pair<std::vector<std::string>, std::vector<std::string>>
split_inputs_impl(std::index_sequence<Is...>) {
    std::pair<std::vector<std::string>, std::vector<std::string>> result;
    ([&]() {
        using W = std::tuple_element_t<Is, InputWrappers>;
        using T = std::tuple_element_t<Is, InputTypes>;
        if constexpr (is_continuous_input_v<W>) {
            result.first.push_back(rfl::type_name_t<T>().str());
        } else {
            result.second.push_back(rfl::type_name_t<T>().str());
        }
    }(), ...);
    return result;
}

template<typename InputWrappers, typename InputTypes>
std::pair<std::vector<std::string>, std::vector<std::string>> split_inputs() {
    static_assert(std::tuple_size_v<InputWrappers> == std::tuple_size_v<InputTypes>);
    return split_inputs_impl<InputWrappers, InputTypes>(
        std::make_index_sequence<std::tuple_size_v<InputWrappers>>{});
}

// Collect command type names from a command tuple.
template<typename CmdTuple, std::size_t... Js>
std::vector<std::string> cmd_type_names_impl(std::index_sequence<Js...>) {
    return { rfl::type_name_t<std::tuple_element_t<Js, CmdTuple>>().str()... };
}

// Build per-output cmd_messages list from OutputTypes tuple.
template<typename OutputTypes, std::size_t... Is>
std::vector<CmdMessagesForOutput> cmd_messages_impl(std::index_sequence<Is...>) {
    std::vector<CmdMessagesForOutput> result;
    ([&]() {
        using T = std::tuple_element_t<Is, OutputTypes>;
        using Cmds = ExtractUserCommands_t<T>;
        if constexpr (std::tuple_size_v<Cmds> > 0) {
            result.push_back({
                .output_index = Is,
                .types = cmd_type_names_impl<Cmds>(
                    std::make_index_sequence<std::tuple_size_v<Cmds>>{}),
            });
        }
    }(), ...);
    return result;
}

template<typename OutputTypes>
std::vector<CmdMessagesForOutput> cmd_messages() {
    return cmd_messages_impl<OutputTypes>(
        std::make_index_sequence<std::tuple_size_v<OutputTypes>>{});
}

} // namespace detail

/// Write the full ModuleDescriptor JSON for a module type.
/// Invoked by --commrat-inspect in module_main; no ModuleType instance is constructed.
template<typename ModuleType>
void write_module_inspect(
    const std::string& module_class,
    const std::string& binary,
    const std::string& outfile)
{
    using IOB  = typename ModuleType::IOBuilder;
    using Meta = typename IOB::Meta;

    auto out_names = detail::output_names<typename Meta::OutputTypes>();
    auto [in_names, synced_names] = detail::split_inputs<
        typename Meta::InputWrappers, typename Meta::InputTypes>();

    std::string exec_mode;
    if constexpr (Meta::is_timer_driven)      exec_mode = "timer";
    else if constexpr (Meta::is_input_driven) exec_mode = "input";
    else                                      exec_mode = "loop";

    std::optional<int64_t> period_ms;
    if constexpr (Meta::is_timer_driven) {
        period_ms = Meta::period.count_ms();
    }

    auto cmds = detail::cmd_messages<typename Meta::OutputTypes>();

    std::optional<std::string> params_defaults;
    if constexpr (ModuleType::has_params) {
        params_defaults = rfl::json::write(typename ModuleType::ParamsType{});
    }

    ModuleDescriptor desc{
        .module_class      = module_class,
        .binary            = binary,
        .outputs           = std::move(out_names),
        .inputs            = std::move(in_names),
        .synced_inputs     = std::move(synced_names),
        .execution_mode    = std::move(exec_mode),
        .default_period_ms = period_ms,
        .cmd_messages      = cmds.empty() ? std::nullopt
                                          : std::optional{std::move(cmds)},
        .params_defaults   = std::move(params_defaults),
    };

    std::ofstream f(outfile);
    f << rfl::json::write(desc) << '\n';
}

} // namespace commrat
