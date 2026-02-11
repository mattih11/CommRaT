#pragma once

/**
 * @file module_core.hpp
 * @brief Core module dependencies - configuration, I/O specs, traits, helpers, metadata
 *
 * Aggregates all fundamental types and utilities needed for module implementation.
 */

#include "commrat/module/io_spec.hpp"
#include "commrat/module/module_config.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include "commrat/module/traits/multi_input_resolver.hpp"
#include "commrat/module/traits/module_types.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/helpers/tims_helpers.hpp"
#include "commrat/module/metadata/input_metadata.hpp"
#include "commrat/module/metadata/input_metadata_accessors.hpp"
#include "commrat/module/metadata/input_metadata_manager.hpp"
