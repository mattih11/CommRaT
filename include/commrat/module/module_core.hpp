#pragma once

/**
 * @file module_core.hpp
 * @brief Core module infrastructure - type traits, specs, and configuration
 * 
 * Aggregates fundamental module building blocks:
 * - I/O specifications (Input, Output, Inputs, Outputs, PeriodicInput, LoopInput)
 * - Type extraction and normalization traits
 * - Module type computations (ModuleTypes)
 * - Configuration structures
 */

#include "commrat/module/io_spec.hpp"
#include "commrat/module/module_config.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include "commrat/module/traits/multi_input_resolver.hpp"
#include "commrat/module/traits/module_types.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/helpers/tims_helpers.hpp"
