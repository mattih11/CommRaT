#pragma once

/**
 * @file module_mixins.hpp
 * @brief Module CRTP mixins - I/O, mailbox, lifecycle, and service components
 *
 * Aggregates all CRTP mixins that provide module functionality through inheritance.
 */

#include "commrat/module/services/subscription.hpp"
#include "commrat/module/services/publishing.hpp"
#include "commrat/module/lifecycle/loop_executor.hpp"
#include "commrat/module/mailbox/mailbox_set.hpp"
#include "commrat/module/io/multi_output_manager.hpp"
#include "commrat/module/lifecycle/command_dispatcher.hpp"
#include "commrat/module/io/multi_input_infrastructure.hpp"
#include "commrat/module/io/multi_input_processor.hpp"
#include "commrat/module/lifecycle/lifecycle_manager.hpp"
#include "commrat/module/lifecycle/work_loop_handler.hpp"
#include "commrat/module/mailbox/mailbox_infrastructure_builder.hpp"
