#pragma once

/**
 * @brief Input interface exports
 * 
 * Three input types supporting different communication patterns:
 * 
 * 1. CmdInput - Command-only interface (no data in process)
 *    - Send commands to remote module
 *    - No subscription, no data reception
 *    - Base class for ContinuousInput and SyncedInput
 * 
 * 2. ContinuousInput - Push model data stream (inherits CmdInput)
 *    - Subscribe to producer
 *    - Receive continuous data stream via data_mbx
 *    - poll_data() → process()
 * 
 * 3. SyncedInput - Pull model timestamp sync (inherits CmdInput)
 *    - get_data(timestamp) queries producer's buffer
 *    - RACK-style getData for multi-rate sensor fusion
 *    - No data_mbx, only RPC to producer
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 */

#include "input/cmd_input.hpp"
#include "input/continuous_input.hpp"
#include "input/synced_input.hpp"