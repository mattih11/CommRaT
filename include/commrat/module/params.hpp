#pragma once

#include <commrat/module/module_config.hpp>
#include <rfl/DefaultIfMissing.hpp>
#include <rfl/json.hpp>
#include <sertial/containers/reflectors.hpp>  // enables rfl for fixed_string, fixed_vector
#include <string>

namespace commrat {

/// Load Params from the opaque blob in ModuleConfig::params.
/// Returns a default-constructed Params if the config has no params field
/// or if JSON parsing fails. Missing fields use their C++ default values
/// (rfl::DefaultIfMissing), so partial JSON overrides work as expected.
///
/// Example:
///   explicit MySensor(const ModuleConfig& config)
///       : Base(config)
///       , params_(commrat::params_from_config<Params>(config))
template<typename ParamsType>
ParamsType params_from_config(const ModuleConfig& config) {
    if (!config.params.has_value()) return ParamsType{};
    std::string json = rfl::json::write(*config.params);
    auto result = rfl::json::read<ParamsType, rfl::DefaultIfMissing>(json);
    return result ? result.value() : ParamsType{};
}

} // namespace commrat
