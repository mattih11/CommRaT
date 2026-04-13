/**
 * @file threading.hpp
 * @brief Unified threading and synchronization abstractions for CommRaT
 * 
 * Provides platform-selectable abstractions for:
 * - Thread creation and management (Thread)
 * - Mutexes and locks (Mutex, SharedMutex)
 * - Condition variables (ConditionVariable)
 * - Thread priorities and CPU affinity (ThreadConfig)
 * 
 * Backend selected at compile time via COMMRAT_PLATFORM_STD or COMMRAT_PLATFORM_EVL.
 * Default: std:: (Linux). EVL: Xenomai 4 hard real-time.
 */

#pragma once

#include "duration.hpp"
#include "platform.hpp"

#include <atomic>
#include <string>
#include <cstdint>

namespace commrat {

// ============================================================================
// Common Types (shared across all backends)
// ============================================================================

/**
 * @brief Thread priority levels
 */
enum class ThreadPriority {
    IDLE = 0,       ///< Idle priority (background tasks)
    LOW = 10,       ///< Low priority
    NORMAL = 50,    ///< Normal priority (default)
    HIGH = 75,      ///< High priority (I/O, event handling)
    REALTIME = 99   ///< Realtime priority (critical paths)
};

/**
 * @brief Thread scheduling policy
 */
enum class SchedulingPolicy {
    NORMAL,         ///< Default OS scheduling (SCHED_OTHER)
    FIFO,           ///< First-in-first-out realtime (SCHED_FIFO)
    ROUND_ROBIN,    ///< Round-robin realtime (SCHED_RR)
    DEADLINE        ///< Deadline scheduling (SCHED_DEADLINE) - future
};

/**
 * @brief Thread configuration
 */
struct ThreadConfig {
    std::string name{"unnamed"};
    ThreadPriority priority = ThreadPriority::NORMAL;
    SchedulingPolicy policy = SchedulingPolicy::NORMAL;
    int cpu_affinity = -1;  ///< -1 = no affinity, >= 0 = pin to CPU
    size_t stack_size = 0;  ///< 0 = default, > 0 = custom stack size
};

/**
 * @brief Condition variable wait status (replaces std::cv_status)
 */
enum class CvStatus {
    NO_TIMEOUT,     ///< Wait completed normally (signaled)
    TIMEOUT         ///< Wait timed out
};

} // namespace commrat

// ============================================================================
// Lock Aliases (must be defined before backend, used by ConditionVariable)
// ============================================================================

// Forward-declare lock types that the backend's ConditionVariable needs.
// The actual Mutex/SharedMutex are defined in the backend.
// We define the lock aliases after the backend include.

// ============================================================================
// Backend Selection
// ============================================================================

#if defined(COMMRAT_PLATFORM_EVL)
    #include "commrat/platform/evl/threading_impl.hpp"
#else
    #include "commrat/platform/std/threading_impl.hpp"
#endif

namespace commrat {

// ============================================================================
// Lock Type Aliases (common, depend on backend-defined Mutex/SharedMutex)
// ============================================================================

/**
 * @brief Scoped lock guard (RAII) - exclusive
 */
using Lock = std::lock_guard<Mutex>;

/**
 * @brief Unique lock (RAII) - exclusive, supports condition variables
 */
using UniqueLock = std::unique_lock<Mutex>;

/**
 * @brief Scoped shared lock (RAII) - for readers
 */
using SharedLock = std::shared_lock<SharedMutex>;

/**
 * @brief Scoped unique lock on SharedMutex (RAII) - for writers
 */
using UniqueLockShared = std::unique_lock<SharedMutex>;

// ============================================================================
// Convenience Macros
// ============================================================================

/**
 * @brief Scoped synchronized block
 * 
 * Usage:
 *   Mutex mtx;
 *   Synchronized(mtx) {
 *     // Critical section
 *   }
 */
#define Synchronized(mutex) \
    if (commrat::Lock _lock_##__LINE__{mutex}; true)

/**
 * @brief Scoped read-locked block (multiple readers)
 */
#define ReadLocked(mutex) \
    if (commrat::SharedLock _lock_##__LINE__{mutex}; true)

/**
 * @brief Scoped write-locked block (exclusive writer)
 */
#define WriteLocked(mutex) \
    if (commrat::UniqueLockShared _lock_##__LINE__{mutex}; true)

} // namespace commrat
