#pragma once

/**
 * @file module_mixins.hpp
 * @brief CRTP mixins that compose Module functionality
 * 
 * Aggregates all CRTP mixin components:
 * - Lifecycle management (start/stop orchestration)
 * - Loop executors (periodic, continuous, loop, multi-input)
 * - Command dispatcher (user command handling)
 * - Multi-output manager (per-output subscribers and work threads)
 * - Multi-input infrastructure (secondary input mailboxes and threads)
 * - Multi-input processor (synchronized input processing)
 * - Work loop handler (subscription protocol messages)
 * - Input metadata (timestamp, sequence, freshness tracking)
 * - Mailbox infrastructure builder (MailboxSet construction)
 */

#include "commrat/module/lifecycle/lifecycle_manager.hpp"
#include "commrat/module/lifecycle/loop_executor.hpp"
#include "commrat/module/lifecycle/command_dispatcher.hpp"
#include "commrat/module/lifecycle/work_loop_handler.hpp"
#include "commrat/module/io/multi_output_manager.hpp"
#include "commrat/module/io/multi_input_infrastructure.hpp"
#include "commrat/module/io/multi_input_processor.hpp"
#include "commrat/module/metadata/input_metadata.hpp"
#include "commrat/module/metadata/input_metadata_accessors.hpp"
#include "commrat/module/metadata/input_metadata_manager.hpp"
#include "commrat/module/mailbox/mailbox_infrastructure_builder.hpp"
