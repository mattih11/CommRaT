#pragma once

#include "commrat/timestamp.hpp"
#include <cstdint>
#include <cstddef>

namespace commrat {

/**
 * @brief Configuration for CmdInput
 * 
 * Minimal config - just needs producer addressing
 */
struct CmdInputConfig {
    uint8_t producer_system_id{0};
    uint8_t producer_instance_id{0};
};

/**
 * @brief Configuration for ContinuousInput
 * 
 * Push model - needs producer addressing + subscription settings
 */
struct ContinuousInputConfig {
    uint8_t producer_system_id{0};
    uint8_t producer_instance_id{0};
    Duration poll_timeout{Milliseconds(100)};  // How long to wait for data
};

/**
 * @brief Configuration for SyncedInput
 * 
 * Pull model - needs producer addressing + getData settings
 * No subscription - queries producer's buffer directly via getData RPC
 */
struct SyncedInputConfig {
    uint8_t producer_system_id{0};
    uint8_t producer_instance_id{0};
    Duration tolerance{Milliseconds(50)};      // Max time difference for getData
    InterpolationMode interpolation{InterpolationMode::NEAREST};
    bool use_shared_work_mbx{true};            // True: use module's work_mbx, False: dedicated mailbox
};

} // namespace commrat
