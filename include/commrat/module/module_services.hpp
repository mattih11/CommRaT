#pragma once

/**
 * @file module_services.hpp
 * @brief Module service components - subscription and publishing
 * 
 * Aggregates runtime service implementations:
 * - Subscription protocol (SubscribeRequest/Reply/Unsubscribe handling)
 * - Publisher (message publishing to subscribers)
 * - MailboxSet (CMD/WORK/PUBLISH mailbox grouping)
 */

#include "commrat/module/services/subscription.hpp"
#include "commrat/module/services/publishing.hpp"
#include "commrat/module/mailbox/mailbox_set.hpp"
