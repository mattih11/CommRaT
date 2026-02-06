#pragma once

/**
 * @file commrat.hpp
 * @brief Main CommRaT header - include this to get everything you need
 * 
 * This header provides:
 * - Module template (automatically includes system messages)
 * - MessageDefinition for defining messages
 * - Message prefixes and subprefixes
 * - All necessary framework components
 * 
 * Users include this + their messages header, that's it!
 */

// Core framework components
#include "commrat/messages.hpp"
#include "commrat/message_id.hpp"
#include "commrat/message_registry.hpp"
#include "commrat/system_registry.hpp"
#include "commrat/tims_wrapper.hpp"
#include "commrat/mailbox.hpp"
#include "commrat/registry_mailbox.hpp"
#include "commrat/registry_module.hpp"

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
 * - Automatic system message inclusion
 * - Clean payload-only user interface
 */
