#pragma once

// Main header that includes all CommRaT components
#include "commrat/messages.hpp"
#include "commrat/message_registry.hpp"
#include "commrat/tims_wrapper.hpp"
#include "commrat/mailbox.hpp"

/**
 * @namespace commrat
 * @brief CommRaT - Modern C++ Real-Time Communication Framework
 * 
 * Integrates RACK's TiMS IPC with SeRTial's reflective serialization
 * to provide compile-time safe, zero-syscall, RT-capable messaging.
 * 
 * Key Features:
 * - Compile-time message type registry for zero-overhead dispatch
 * - Template-based serialization with automatic type validation
 * - Runtime visitor pattern for dynamic message handling
 * - Zero-allocation design using SeRTial containers
 */
