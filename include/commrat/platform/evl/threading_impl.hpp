/**
 * @file evl/threading_impl.hpp
 * @brief EVL (Xenomai 4 / libevl) threading backend for CommRaT [SKELETON]
 * 
 * Implements Thread, Mutex, SharedMutex, ConditionVariable using
 * EVL out-of-band (OOB) primitives for hard real-time guarantees.
 * 
 * This file is included by commrat/platform/threading.hpp when
 * COMMRAT_PLATFORM_EVL is defined.
 * 
 * NOT YET IMPLEMENTED - requires libevl headers and Xenomai 4 kernel.
 * See docs/work/EVL_API_REFERENCE.md for implementation details.
 * 
 * EVL Primitives Used:
 * - Thread:    pthread_create + evl_attach_self() for OOB scheduling
 * - Mutex:     evl_mutex (priority inheritance by default)
 * - SharedMutex: evl_rwlock (writer-biased, no PI)
 * - CondVar:   evl_event paired with evl_mutex
 * 
 * WARNING: Any glibc/kernel syscall from OOB context demotes to in-band.
 * Forbidden in OOB: malloc, new, delete, std::cout, throw, POSIX mutexes.
 * Only libevl functions + memcpy/std::atomic are safe in OOB context.
 */

#pragma once

#error "COMMRAT_PLATFORM_EVL is not yet implemented. See docs/work/EVL_API_REFERENCE.md"

// Future implementation structure (for reference):
//
// #include <evl/thread.h>
// #include <evl/mutex.h>
// #include <evl/rwlock.h>
// #include <evl/event.h>
// #include <evl/clock.h>
// #include <evl/sched.h>
//
// namespace commrat {
//
// class Thread {
//     // pthread_create + evl_attach_self("thread_name", ...)
//     // evl_set_schedattr(evl_fd, &SCHED_FIFO attr)
//     // evl_detach_self() on exit
// };
//
// class Mutex {
//     // evl_mutex with EVL_MUTEX_NORMAL | priority inheritance
//     // evl_create_mutex(&mutex, clock, name, flags)
//     // evl_lock_mutex(&mutex) / evl_unlock_mutex(&mutex)
// };
//
// class SharedMutex {
//     // evl_rwlock (writer-biased, no PI)
//     // evl_create_rwlock(&rwlock, clock, name, flags)
//     // evl_read_lock(&rwlock) / evl_write_lock(&rwlock)
// };
//
// class ConditionVariable {
//     // evl_event paired with evl_mutex
//     // evl_create_event(&event, clock, name, flags)
//     // evl_wait_event(&event, &mutex) / evl_signal_event(&event)
//     // evl_timedwait_event for wait_for()
// };
//
// } // namespace commrat
