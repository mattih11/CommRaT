#pragma once

#include <cstdint>

namespace commrat {

/**
 * @brief Mailbox type enumeration
 * 
 * Defines the different types of mailboxes used in the CommRaT framework.
 * These values are used as mailbox index offsets.
 */
enum class MailboxType : uint8_t {
    CMD = 0,         // Command/subscription mailbox
    WORK = 16,       // Subscription protocol (legacy, to be merged with CMD)
    PUBLISH = 32,    // Publishing to subscribers (legacy, to be merged with CMD)
    DATA = 48        // Input data reception (base index, actual is DATA+N)
};

} // namespace commrat
