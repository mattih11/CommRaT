#pragma once

/**
 * @brief Output interface export
 * 
 * BufferedOutput - The single output type for all use cases:
 * - Stores recent data in RingBuffer
 * - Command mailbox receives system commands (Subscribe/Unsubscribe)
 * - Command mailbox receives user-defined commands (SetRate, etc.)
 * - Supports publish() for ContinuousInput consumers
 * - Supports get_data(timestamp) for SyncedInput consumers
 * - Address depends on output type_id (must be per-output)
 * 
 * All outputs are buffered - no streaming-only variant needed.
 * Buffer supports both continuous publishing AND getData queries.
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 */

#include "output/buffered_output.hpp"