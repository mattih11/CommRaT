/**
 * @file timestamp.hpp
 * @brief Unified timestamp and time utility abstractions for CommRaT
 * 
 * Provides platform-selectable abstractions for:
 * - Getting current time (via Time::now())
 * - Sleeping (Time::sleep(), Time::sleep_until(), Time::yield())
 * - Converting between time units
 * - Timestamp comparisons and arithmetic
 * 
 * Includes duration.hpp which defines the Duration type, free function
 * constructors (Milliseconds(), Seconds(), ...), and user-defined literals.
 * 
 * Backend selected at compile time via COMMRAT_PLATFORM_STD or COMMRAT_PLATFORM_EVL.
 */

#pragma once

#include "duration.hpp"
#include "platform.hpp"

#include <cstdint>

namespace commrat {

/**
 * @brief Timestamp type - uint64_t nanoseconds since epoch
 * 
 * Compatible with TiMS timestamp format.
 * Range: ~584 years from epoch (sufficient for all practical purposes)
 */
using Timestamp = uint64_t;

} // namespace commrat

// ============================================================================
// Backend Selection (Time class implementation)
// ============================================================================

#if defined(COMMRAT_PLATFORM_EVL)
    #include "commrat/platform/evl/timestamp_impl.hpp"
#else
    #include "commrat/platform/std/timestamp_impl.hpp"
#endif
