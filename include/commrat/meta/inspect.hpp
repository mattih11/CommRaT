#pragma once

#include <commrat/meta/descriptor.hpp>
#include <commrat/messaging/data_with_commands.hpp>
#include <commrat/messaging/registry_utils.hpp>
#include <commrat/module/io/io_spec.hpp>
#include <commrat/module/helpers/address_helpers.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
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

template<typename Types>
std::vector<std::string> type_names() {
    return output_names<Types>();
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
template<typename Registry, typename OutputTypes, std::size_t... Is>
std::vector<CmdMessagesForOutput> cmd_messages_impl(std::index_sequence<Is...>) {
    std::vector<CmdMessagesForOutput> result;
    ([&]() {
        using T = std::tuple_element_t<Is, OutputTypes>;
        using Cmds = registry::get_commands_for_message_defs_t<
            T, typename Registry::InspectionMessageDefs>;
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

template<typename Registry, typename OutputTypes>
std::vector<CmdMessagesForOutput> cmd_messages() {
    return cmd_messages_impl<Registry, OutputTypes>(
        std::make_index_sequence<std::tuple_size_v<OutputTypes>>{});
}

template<typename Registry, typename CmdTuple, std::size_t... Is>
std::vector<CommandMessageDescriptor> command_descriptors_impl(
    std::index_sequence<Is...>) {
    return {
        CommandMessageDescriptor{
            .request_type = rfl::type_name_t<std::tuple_element_t<Is, CmdTuple>>().str(),
            .request_id = Registry::template get_message_id<
                std::tuple_element_t<Is, CmdTuple>>(),
            .reply_type = rfl::type_name_t<
                typename std::tuple_element_t<Is, CmdTuple>::Reply>().str(),
            .reply_id = Registry::template get_message_id<
                typename std::tuple_element_t<Is, CmdTuple>::Reply>(),
        }...
    };
}

template<typename Registry, typename OutputTypes, std::size_t... Is>
std::vector<CommandEndpointDescriptor> command_endpoints_impl(
    std::index_sequence<Is...>) {
    std::vector<CommandEndpointDescriptor> result;
    ([&]() {
        using OutputType = std::tuple_element_t<Is, OutputTypes>;
        using Commands = registry::get_commands_for_message_defs_t<
            OutputType, typename Registry::InspectionMessageDefs>;
        if constexpr (std::tuple_size_v<Commands> > 0) {
            constexpr uint32_t output_id = Registry::template get_message_id<OutputType>();
            result.push_back({
                .output_index = Is,
                .output_type = inspect_type_name<OutputType>(),
                .output_message_id = output_id,
                .address_type_id = static_cast<uint8_t>(output_id & 0xFF),
                .mailbox_index = CMD_MBX_BASE,
                .commands = command_descriptors_impl<Registry, Commands>(
                    std::make_index_sequence<std::tuple_size_v<Commands>>{}),
            });
        }
    }(), ...);
    return result;
}

template<typename Registry, typename OutputTypes>
std::vector<CommandEndpointDescriptor> command_endpoints() {
    return command_endpoints_impl<Registry, OutputTypes>(
        std::make_index_sequence<std::tuple_size_v<OutputTypes>>{});
}

template<typename Registry, typename OutputTypes>
LifecycleEndpointDescriptor lifecycle_endpoint() {
    if constexpr (std::tuple_size_v<OutputTypes> == 0) {
        return {
            .anchor_output_index = std::nullopt,
            .anchor_message_id = 0,
            .address_type_id = 0,
            .mailbox_index = LIFECYCLE_MBX_INDEX,
        };
    } else {
        using PrimaryOutput = std::tuple_element_t<0, OutputTypes>;
        constexpr uint32_t output_id = Registry::template get_message_id<PrimaryOutput>();
        return {
            .anchor_output_index = 0,
            .anchor_message_id = output_id,
            .address_type_id = static_cast<uint8_t>(output_id & 0xFF),
            .mailbox_index = LIFECYCLE_MBX_INDEX,
        };
    }
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
    auto remote_names = detail::type_names<typename Meta::RemoteTypes>();

    std::string exec_mode;
    if constexpr (Meta::is_timer_driven)      exec_mode = "timer";
    else if constexpr (Meta::is_input_driven) exec_mode = "input";
    else                                      exec_mode = "loop";

    std::optional<int64_t> period_ms;
    if constexpr (Meta::is_timer_driven) {
        period_ms = Meta::period.count_ms();
    }

    auto cmds = detail::cmd_messages<
        typename ModuleType::RegistryType, typename Meta::OutputTypes>();
    auto command_endpoints = detail::command_endpoints<
        typename ModuleType::RegistryType, typename Meta::OutputTypes>();

    std::optional<rfl::Generic> params_defaults;
    if constexpr (ModuleType::has_params) {
        auto json = rfl::json::write(typename ModuleType::ParamsType{});
        auto parsed = rfl::json::read<rfl::Generic>(json);
        if (parsed) params_defaults = parsed.value();
    }

    ModuleDescriptor desc{
        .module_class      = module_class,
        .binary            = binary,
        .outputs           = std::move(out_names),
        .inputs            = std::move(in_names),
        .synced_inputs     = std::move(synced_names),
        .remotes           = std::move(remote_names),
        .execution_mode    = std::move(exec_mode),
        .default_period_ms = period_ms,
        .cmd_messages      = cmds.empty() ? std::nullopt
                                          : std::optional{std::move(cmds)},
        .command_endpoints = command_endpoints.empty()
            ? std::nullopt
            : std::optional{std::move(command_endpoints)},
        .lifecycle_endpoint = detail::lifecycle_endpoint<
            typename ModuleType::RegistryType, typename Meta::OutputTypes>(),
        .params_defaults   = std::move(params_defaults),
    };

    const std::filesystem::path output_path(outfile);
    std::filesystem::path temporary_path = output_path;
    temporary_path += ".tmp";

    try {
        std::ofstream file(temporary_path, std::ios::trunc);
        if (!file) {
            throw std::runtime_error(
                "cannot open temporary descriptor '" + temporary_path.string() + "'");
        }

        file << rfl::json::write(desc) << '\n';
        file.close();
        if (!file) {
            throw std::runtime_error(
                "cannot write temporary descriptor '" + temporary_path.string() + "'");
        }

        std::filesystem::rename(temporary_path, output_path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        throw;
    }
}

} // namespace commrat
