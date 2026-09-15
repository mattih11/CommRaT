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
    CMD = 0,          // Per-output command/subscription mailbox
    WORK = 1,         // Outbound RPC and reply mailbox
    PUBLISH = 2,      // Output publishing mailbox
    DATA = 3,         // Input data reception (base index, actual is DATA+N)
    LIFECYCLE = 0xFF  // Module-level on/off/status mailbox
};

} // namespace commrat
