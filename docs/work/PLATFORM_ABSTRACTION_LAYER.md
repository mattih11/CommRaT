# Platform Abstraction Layer

**Status**: Planned  
**Priority**: High  
**Created**: February 12, 2026

## Overview

Complete transition to platform-agnostic abstractions for threading, timing, synchronization, and other OS-dependent operations. This enables compilation against different real-time platforms (standard Linux, libevl, etc.) by swapping the platform layer without changing application code.

## Current State

Already implemented abstractions (in `include/commrat/threading.hpp`, `include/commrat/timestamp.hpp`):
- `Thread` (wraps `std::thread` or platform equivalent)
- `Mutex`, `SharedMutex` (wraps `std::mutex`, `std::shared_mutex`)
- `Lock`, `SharedLock` (wraps `std::lock_guard`, `std::shared_lock`)
- `Timestamp`, `Duration` (wraps `std::chrono` types)
- `Time::now()`, `Time::sleep()` (timing operations)

## Motivation

**libevl** (EVL - Enhanced Virtual Linux) provides hard real-time guarantees via:
- Out-of-band execution (OOB) for time-critical threads
- Real-time safe synchronization primitives
- Predictable scheduling (no preemption by kernel)
- Microsecond-level precision

Standard `std::thread` and `std::mutex` are not sufficient for hard real-time requirements.

## Remaining Work

### 1. Condition Variables
- Abstract `std::condition_variable` → `ConditionVariable`
- libevl equivalent: `evl_cond`

### 2. Thread Attributes
- Priority setting (real-time priorities)
- CPU affinity (pin threads to specific cores)
- Stack size configuration
- libevl: OOB thread creation, core scheduling

### 3. Atomic Operations
- Currently using `std::atomic<T>` directly
- May need platform-specific atomics for exotic architectures
- Low priority (std::atomic is typically sufficient)

### 4. Memory Allocation
- Abstract allocator for real-time safe memory
- libevl: `evl_alloc_heap`, `evl_alloc_shared`
- Pre-allocated memory pools

### 5. File I/O
- Abstract file operations for logger, config loading
- Real-time safe I/O (async writes, memory-mapped files)

## Design

### Platform Selection

Compile-time selection via CMake:

```cmake
option(COMMRAT_PLATFORM "Platform backend" "std")
# Options: "std" (standard Linux), "evl" (libevl), "rtai", "xenomai"
```

### Header Structure

```cpp
// include/commrat/platform/platform.hpp
#if defined(COMMRAT_PLATFORM_EVL)
    #include <commrat/platform/evl/thread.hpp>
    #include <commrat/platform/evl/mutex.hpp>
    #include <commrat/platform/evl/timestamp.hpp>
#else
    #include <commrat/platform/std/thread.hpp>
    #include <commrat/platform/std/mutex.hpp>
    #include <commrat/platform/std/timestamp.hpp>
#endif
```

### Example: Thread Abstraction

```cpp
// include/commrat/platform/std/thread.hpp
class Thread {
    std::thread thread_;
public:
    template<typename Func>
    Thread(Func&& func) : thread_(std::forward<Func>(func)) {}
    
    void join() { thread_.join(); }
    void detach() { thread_.detach(); }
};

// include/commrat/platform/evl/thread.hpp
class Thread {
    int efd_;  // EVL thread descriptor
public:
    template<typename Func>
    Thread(Func&& func) {
        // Create OOB thread with evl_attach_thread
    }
    
    void join() { /* EVL-specific join */ }
    void detach() { /* EVL-specific detach */ }
    
    void set_priority(int prio) { /* EVL scheduling */ }
};
```

## Implementation Plan

**Phase 1**: Extract remaining `std::` uses
- Audit codebase for direct `std::thread`, `std::mutex` usage
- Replace with CommRaT abstractions
- Estimated: 1 week

**Phase 2**: Implement libevl backend
- Implement evl::Thread, evl::Mutex, etc.
- Testing on libevl-enabled kernel
- Estimated: 2-3 weeks

**Phase 3**: Performance validation
- Benchmark latency, jitter, context switch times
- Compare std vs evl platforms
- Document real-time performance gains
- Estimated: 1 week

## Benefits

1. **Hard Real-Time Support**: Enable libevl for deterministic execution
2. **Platform Portability**: Easy adaptation to new real-time platforms
3. **Testing Flexibility**: Develop on standard Linux, deploy on real-time
4. **Future-Proof**: Insulates application code from platform changes

## Related Work

- Threading abstractions: `include/commrat/threading.hpp`
- Timestamp abstractions: `include/commrat/timestamp.hpp`
- Real-time constraints: `.github/copilot-instructions.md`
