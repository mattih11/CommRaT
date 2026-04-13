# libevl (Xenomai 4) API Reference for CommRaT Platform Abstraction

**Status**: Active Reference
**Source**: https://v4.xenomai.org/core/user-api/index.html
**Purpose**: Detailed EVL API reference for implementing the `evl/` platform backend.
When working on platform abstraction code, read this file for function signatures, error codes, usage patterns, and gotchas.

---

## Table of Contents

1. [EVL Execution Model](#evl-execution-model)
2. [Initialization](#initialization)
3. [Thread API](#thread-api)
4. [Mutex API](#mutex-api)
5. [Read/Write Lock API](#readwrite-lock-api)
6. [Event API (Condition Variable)](#event-api-condition-variable)
7. [Clock API](#clock-api)
8. [Scheduling API](#scheduling-api)
9. [CommRaT Mapping Table](#commrat-mapping-table)
10. [Implementation Notes](#implementation-notes)

---

## EVL Execution Model

### Two Execution Stages

EVL threads run on two execution stages:

- **Out-of-band (OOB)**: Scheduled by EVL core with ultra-low latency. This is where real-time code runs.
- **In-band**: Scheduled by the regular Linux kernel. Used for initialization, cleanup, and non-RT operations.

### Key Rules

1. An EVL thread starts as a regular POSIX thread (`pthread_create`), then attaches to EVL core via `evl_attach_self()`.
2. Once attached, the thread can switch between OOB and in-band stages.
3. **Any call to glibc/kernel syscalls from OOB context automatically demotes the thread to in-band**, losing RT guarantees.
4. Only `libevl` services and a small subset of C library routines (memcpy, strcpy, etc.) are safe from OOB context.
5. `malloc()`, `stdio`, `std::cout`, exceptions, and any allocating C++ constructor are FORBIDDEN from OOB context.

### Thread Lifetime Pattern

```
1. main() or pthread:  Normal in-band execution (glibc OK)
2. evl_attach_self():  Bind thread to EVL core
3. Time-critical loop:  ONLY libevl calls (OOB execution)
4. evl_detach_self():  Unbind (optional, auto on exit)
5. Cleanup:            Normal in-band execution (glibc OK)
```

---

## Initialization

### Header: `<evl/evl.h>`

### `evl_init()`

```c
int evl_init(void);
```

Bootstraps EVL services for the current process. Must be called before creating any EVL objects (except threads, which trigger implicit init via `evl_attach_self()`). Only the first call applies; subsequent calls are silently ignored.

**Actions performed:**
- Resolves vDSO address for CPU architecture
- Calls `mlockall()` to lock all process memory (prevents page faults)
- Connects to EVL core in kernel space
- Validates ABI compatibility
- Initializes built-in proxy streams (for `evl_printf()`)

**Returns:** 0 on success, negated error code on failure:
- `-EPERM`: Cannot call `mlockall()` (need `CAP_IPC_LOCK` or root)
- `-ENOMEM`: Cannot lock memory
- `-ENOSYS`: EVL core not enabled in kernel
- `-ENOEXEC`: ABI mismatch (rebuild libevl against current kernel UAPI)

**CommRaT usage:** Call once in `Module2::initialize()` or application startup when `COMMRAT_PLATFORM_EVL` is defined.

---

## Thread API

### Header: `<evl/thread.h>`

EVL does NOT create threads. It extends regular POSIX threads with OOB scheduling capability.

### `evl_attach_self()`

```c
int evl_attach_self(const char *fmt, ...);
```

Shorthand for `evl_attach_thread(EVL_CLONE_PRIVATE, fmt, ...)`. Attaches the calling POSIX thread to the EVL core, enabling it to use EVL real-time services.

**Actions:**
- Binds thread to EVL core
- Pins thread to current CPU (can change later with `sched_setaffinity`)
- Translates current in-band scheduling params to EVL equivalents:
  - `SCHED_OTHER/BATCH/IDLE, 0` -> `SCHED_WEAK, 0` (stays in-band after attach)
  - `SCHED_FIFO/RR, prio` -> `SCHED_FIFO, prio` (goes OOB after attach)
- Triggers implicit `evl_init()` if not already called

**Returns:** File descriptor for the thread on success (use for `evl_set_schedattr()` etc.), negated error on failure:
- `-EEXIST`: Name conflict
- `-EPERM`: Cannot `mlockall()`
- `-ENOMEM`: Cannot lock memory
- `-ENOSYS`: EVL core not in kernel

**Name convention:** If name starts with `/`, forces `EVL_CLONE_PUBLIC`. Include PID for uniqueness: `"commrat-data:%d", getpid()`.

### `evl_attach_thread()`

```c
int evl_attach_thread(int flags, const char *fmt, ...);
```

Full form. Flags:
- `EVL_CLONE_PUBLIC`: Visible in `/dev/evl/thread/` (for cross-process access)
- `EVL_CLONE_PRIVATE`: Process-local only
- `EVL_CLONE_OBSERVABLE`: Enable health monitoring observable
- `EVL_CLONE_NONBLOCK`: Set fd to `O_NONBLOCK`

### `evl_detach_self()`

```c
int evl_detach_self(void);
```

Detaches calling thread from EVL core. Thread can no longer use EVL services. Automatic on thread exit. Returns 0 on success.

### `evl_get_self()`

```c
int evl_get_self(void);
```

Returns the file descriptor for the calling thread (obtained from `evl_attach_self()`). Useful for `evl_set_schedattr()`, `evl_get_state()` etc.

Returns `-EPERM` if not attached.

### `evl_switch_oob()` / `evl_switch_inband()`

```c
int evl_switch_oob(void);
int evl_switch_inband(void);
```

Manually force execution stage. Rarely needed -- EVL services do this automatically. Heavy operation (two context switches). Only use when entering a syscall-free code section that must be OOB.

### `evl_is_inband()`

```c
bool evl_is_inband(void);
```

Check current execution stage. Non-attached threads always return true.

### `evl_get_state()`

```c
int evl_get_state(int efd, struct evl_thread_state *statebuf);
```

Extended thread info:
```c
struct evl_thread_state {
    struct evl_sched_attrs eattrs;  // Scheduling attrs (may reflect PI boost)
    __u32 cpu;      // Current CPU
    __u32 state;    // Internal state bitmask
    __u32 isw;      // In-band switch count (should be 0 in RT loop!)
    __u32 csw;      // Context switch count
    __u32 sc;       // EVL syscall count
    __u32 rwa;      // Remote wakeup count (IPI overhead indicator)
    __u64 xtime;    // Cumulated CPU time (nanoseconds)
};
```

**CommRaT usage:** Monitor `isw` counter to detect accidental in-band switches in RT loops.

### `evl_unblock_thread()`

```c
int evl_unblock_thread(int efd);
```

Force-unblock a thread sleeping on any EVL monitor element. The blocked call returns `-EINTR`. Useful for shutdown signaling.

### Health Monitoring

```c
int evl_set_thread_mode(int efd, int mask, int *oldmask);
int evl_clear_thread_mode(int efd, int mask, int *oldmask);
```

Mode bits for detecting RT violations:
- `EVL_T_WOSS`: Warn on Stage Switch (detects accidental in-band demotion)
- `EVL_T_WOLI`: Warn on Locking Inconsistency
- `EVL_T_WOSX`: Warn on Stage eXclusion
- `EVL_T_WOSO`: Warn on Schedule Overrun
- `EVL_T_HMSIG`: Deliver HM events via `SIGDEBUG` signal
- `EVL_T_HMOBS`: Deliver HM events via thread's observable

Diagnostic codes:
- `EVL_HMDIAG_SIGDEMOTE`: Demoted due to POSIX signal
- `EVL_HMDIAG_SYSDEMOTE`: Demoted due to in-band syscall
- `EVL_HMDIAG_WATCHDOG`: CPU hogged too long (default 4s)
- `EVL_HMDIAG_LKDEPEND`: Mutex owner switching in-band (priority inversion)
- `EVL_HMDIAG_LKIMBALANCE`: Unlock of free mutex
- `EVL_HMDIAG_OVERRUN`: Schedule overrun (SCHED_TP)

**CommRaT usage:** Enable `EVL_T_WOSS | EVL_T_HMSIG` for all RT threads to detect accidental glibc calls.

---

## Mutex API

### Header: `<evl/mutex.h>`

EVL mutexes support priority inheritance (default) or priority ceiling. Semantics close to POSIX.

### `evl_create_mutex()`

```c
int evl_create_mutex(struct evl_mutex *mutex, int clockfd, unsigned int ceiling,
                     int flags, const char *fmt, ...);
```

- `mutex`: In-memory descriptor (stack or static, NOT heap-allocated)
- `clockfd`: Clock for timed operations (`EVL_CLOCK_MONOTONIC` recommended)
- `ceiling`: 0 = priority inheritance (PI), 1-99 = priority ceiling value
- `flags`: `EVL_MUTEX_NORMAL` or `EVL_MUTEX_RECURSIVE`, plus visibility flags

**Returns:** File descriptor on success, negated error on failure.

### `evl_new_mutex()`

```c
int evl_new_mutex(struct evl_mutex *mutex, const char *fmt, ...);
```

Shorthand: creates normal, non-recursive, PI-enabled, private mutex on `EVL_CLOCK_MONOTONIC`. Equivalent to:
```c
evl_create_mutex(mutex, EVL_CLOCK_MONOTONIC, 0,
                 EVL_MUTEX_NORMAL | EVL_CLONE_PRIVATE, fmt, ...);
```

### Static Initialization

```c
struct evl_mutex mutex = EVL_MUTEX_INITIALIZER("name", EVL_CLOCK_MONOTONIC, 0, EVL_MUTEX_NORMAL|EVL_CLONE_PRIVATE);
```

Lazy binding: actual creation deferred until first lock attempt (may cause in-band transition).

### `evl_lock_mutex()`

```c
int evl_lock_mutex(struct evl_mutex *mutex);
```

Exclusive lock. Blocks until available. Priority-ordered wait queue (highest priority, longest waiting first). Returns 0 on success.

**CRITICAL:** While holding an EVL mutex, switching to in-band is wrong (priority inversion). `SCHED_WEAK` threads are kept OOB until last mutex released.

### `evl_timedlock_mutex()`

```c
int evl_timedlock_mutex(struct evl_mutex *mutex, const struct timespec *timeout);
```

Like `evl_lock_mutex()` with absolute timeout. Returns `-ETIMEDOUT` on expiry.

### `evl_trylock_mutex()`

```c
int evl_trylock_mutex(struct evl_mutex *mutex);
```

Non-blocking attempt. Returns 0 on success, `-EBUSY` if locked by another thread.

### `evl_unlock_mutex()`

```c
int evl_unlock_mutex(struct evl_mutex *mutex);
```

Release. Only the owning thread may unlock. Returns `-EPERM` if caller doesn't own it. For recursive mutexes, decrements nesting count.

### `evl_close_mutex()`

```c
int evl_close_mutex(struct evl_mutex *mutex);
```

Dispose of mutex, release file descriptor. Kept alive in core until all open references released.

### Priority Ceiling

```c
int evl_set_mutex_ceiling(struct evl_mutex *mutex, unsigned int ceiling); // 1-99
int evl_get_mutex_ceiling(struct evl_mutex *mutex);
```

---

## Read/Write Lock API

### Header: `<evl/rwlock.h>`

Process-local only (no cross-process sharing). Writer-biased: waiting writer is granted access before new readers. Up to 2^30 concurrent readers.

**WARNING:** No priority inheritance. May cause priority inversion. Use plain mutex with PI for better performance unless read-mostly workload with equal-priority threads.

### `evl_create_rwlock()` / `evl_new_rwlock()`

```c
int evl_create_rwlock(struct evl_rwlock *rwlock);
int evl_new_rwlock(struct evl_rwlock *rwlock); // alias
```

Returns 0 on success.

### Static Initialization

```c
struct evl_rwlock rwlock = EVL_RWLOCK_INITIALIZER();
// or
static DEFINE_EVL_RWLOCK(my_rwlock);
```

### Read-Side Operations

```c
int evl_lock_read(struct evl_rwlock *rwlock);     // Blocking, nestable
int evl_trylock_read(struct evl_rwlock *rwlock);   // Non-blocking (-EAGAIN if unavailable)
int evl_unlock_read(struct evl_rwlock *rwlock);    // Must pair with lock_read/trylock_read
```

### Write-Side Operations

```c
int evl_lock_write(struct evl_rwlock *rwlock);     // Blocking, exclusive
int evl_trylock_write(struct evl_rwlock *rwlock);  // Non-blocking (-EAGAIN if unavailable)
int evl_unlock_write(struct evl_rwlock *rwlock);   // Must pair with lock_write/trylock_write
```

### `evl_destroy_rwlock()`

```c
int evl_destroy_rwlock(struct evl_rwlock *rwlock);
```

Sleeping threads receive `-EIDRM`.

**CommRaT mapping:** `SharedMutex` -> `evl_rwlock`. Note: no PI support. Consider if `evl_mutex` with PI is sufficient for CommRaT's ring buffer access pattern.

---

## Event API (Condition Variable)

### Header: `<evl/event.h>`

EVL events = POSIX condition variables. Must be paired with an EVL mutex. Uses wait morphing optimization (waiter resumes when mutex is released, not when signaled).

### `evl_create_event()`

```c
int evl_create_event(struct evl_event *evt, int clockfd, int flags,
                     const char *fmt, ...);
```

- `clockfd`: For timed waits (`EVL_CLOCK_MONOTONIC`)
- `flags`: Visibility (`EVL_CLONE_PUBLIC`/`PRIVATE`) + `EVL_CLONE_NONBLOCK`

### `evl_new_event()`

```c
int evl_new_event(struct evl_event *evt, const char *fmt, ...);
```

Shorthand: private event on monotonic clock.

### Static Initialization

```c
struct evl_event event = EVL_EVENT_INITIALIZER("name", EVL_CLOCK_MONOTONIC, EVL_CLONE_PRIVATE);
```

### Wait Operations

```c
int evl_wait_event(struct evl_event *evt, struct evl_mutex *mutex);
int evl_timedwait_event(struct evl_event *evt, struct evl_mutex *mutex,
                        const struct timespec *timeout);
```

Standard condition variable semantics:
1. Caller must hold `mutex`
2. Call atomically releases mutex and blocks
3. On wakeup, mutex is re-acquired before return
4. Subject to spurious wakeups -- always use while-loop pattern

Returns 0 on success, `-ETIMEDOUT` for timed variant, `-EINTR` if unblocked, `-EBADFD` if mixed mutexes on same event.

### Signal Operations

```c
int evl_signal_event(struct evl_event *evt);         // Wake highest-priority waiter
int evl_signal_thread(struct evl_event *evt, int thrfd); // Wake specific thread
int evl_broadcast_event(struct evl_event *evt);       // Wake all waiters
```

**CRITICAL:** Must hold the paired mutex when signaling. Actual wakeup happens when mutex is released (wait morphing).

### Usage Pattern

```c
struct evl_mutex lock = EVL_MUTEX_INITIALIZER(...);
struct evl_event event = EVL_EVENT_INITIALIZER(...);
bool condition = false;

void waiter(void) {
    evl_lock_mutex(&lock);
    while (!condition)
        evl_wait_event(&event, &lock);
    // condition is true, lock is held
    evl_unlock_mutex(&lock);
}

void poster(void) {
    evl_lock_mutex(&lock);
    condition = true;
    evl_signal_event(&event);
    evl_unlock_mutex(&lock);  // actual wakeup happens here
}
```

### `evl_close_event()`

```c
int evl_close_event(struct evl_event *evt);
```

---

## Clock API

### Header: `<evl/clock.h>`

### Built-in Clocks

- `EVL_CLOCK_MONOTONIC`: Same as `CLOCK_MONOTONIC`. Use for elapsed time measurements. Cannot be set.
- `EVL_CLOCK_REALTIME`: Same as `CLOCK_REALTIME`. Wall clock, can be adjusted (NTP). Avoid for timing.

### `evl_read_clock()`

```c
int evl_read_clock(int clockfd, struct timespec *tp);
```

Read current time. Returns 0 on success.

**CommRaT mapping:** `Time::now()` -> `evl_read_clock(EVL_CLOCK_MONOTONIC, &ts)` then convert to nanoseconds.

### `evl_sleep_until()`

```c
int evl_sleep_until(int clockfd, const struct timespec *timeout);
```

Absolute sleep until specified time. Only accepts absolute timespec. Returns 0 on success, `-EINTR` on unblock.

### `evl_usleep()`

```c
int evl_usleep(useconds_t usecs);
```

Relative sleep (microseconds). Max 1,000,000 (1 second). Uses `EVL_CLOCK_MONOTONIC` internally.

**CommRaT mapping:** `Time::sleep()` -> `evl_usleep()` for short sleeps, or `evl_sleep_until()` for precise periodic timing.

### `evl_get_clock_resolution()`

```c
int evl_get_clock_resolution(int clockfd, struct timespec *tp);
```

---

## Scheduling API

### Header: `<evl/sched.h>`

### Scheduling Policies (priority order)

1. `SCHED_FIFO`: Fixed priority, preemptive (priorities 1-99)
2. `SCHED_RR`: Round-robin with per-thread quantum (priorities 1-99)
3. `SCHED_TP`: Temporal partitioning (cycle-based, non-overlapping time windows)
4. `SCHED_QUOTA`: CPU budget enforcement per thread group
5. `SCHED_WEAK`: Non-RT, runs in-band, can occasionally use EVL services (slow)
6. `SCHED_IDLE`: Internal only, not user-accessible

### `evl_set_schedattr()`

```c
int evl_set_schedattr(int efd, const struct evl_sched_attrs *attrs);
```

Set scheduling attributes for thread `efd`.

```c
// SCHED_FIFO example
struct evl_sched_attrs attrs;
attrs.sched_policy = SCHED_FIFO;
attrs.sched_priority = 80;  // 1-99
evl_set_schedattr(efd, &attrs);

// SCHED_RR example
attrs.sched_policy = SCHED_RR;
attrs.sched_priority = 50;
attrs.sched_rr_quantum = (struct timespec){.tv_sec = 0, .tv_nsec = 1000000}; // 1ms
evl_set_schedattr(efd, &attrs);
```

**IMPORTANT:** `pthread_setschedparam()` does NOT affect EVL scheduling. Use only `evl_set_schedattr()` after attachment.

### `evl_get_schedattr()`

```c
int evl_get_schedattr(int efd, struct evl_sched_attrs *attrs);
```

Returns base priority (not reflecting PI boost).

### `evl_yield()`

```c
int evl_yield(void);
```

Manual round-robin: move to end of priority group. Only useful for `SCHED_FIFO`/`SCHED_RR`.

### CommRaT Priority Mapping

```
CommRaT ThreadPriority -> EVL SCHED_FIFO priority
  IDLE(0)      -> SCHED_WEAK, 0
  LOW(10)      -> SCHED_FIFO, 10
  NORMAL(50)   -> SCHED_FIFO, 50
  HIGH(75)     -> SCHED_FIFO, 75
  REALTIME(99) -> SCHED_FIFO, 99
```

---

## CommRaT Mapping Table

Complete mapping of CommRaT abstractions to std:: and EVL equivalents:

| CommRaT Type | std:: Backend | EVL Backend | Notes |
|---|---|---|---|
| **Thread** | `std::thread` | `pthread_create` + `evl_attach_self()` | EVL extends POSIX threads |
| **~Thread** | `thread.join()` | `evl_detach_self()` + `pthread_join()` | Detach before join |
| **Mutex** | `std::mutex` | `struct evl_mutex` + `evl_new_mutex()` | EVL has PI by default |
| **Mutex::lock()** | `mutex.lock()` | `evl_lock_mutex(&mutex)` | Priority-ordered wait |
| **Mutex::try_lock()** | `mutex.try_lock()` | `evl_trylock_mutex(&mutex)` | Returns -EBUSY (not bool) |
| **Mutex::unlock()** | `mutex.unlock()` | `evl_unlock_mutex(&mutex)` | Only owner can unlock |
| **SharedMutex** | `std::shared_mutex` | `struct evl_rwlock` + `evl_create_rwlock()` | No PI! Writer-biased |
| **SharedMutex::lock()** | `mutex.lock()` | `evl_lock_write(&rwlock)` | Exclusive |
| **SharedMutex::lock_shared()** | `mutex.lock_shared()` | `evl_lock_read(&rwlock)` | Shared |
| **SharedMutex::unlock()** | `mutex.unlock()` | `evl_unlock_write(&rwlock)` | |
| **SharedMutex::unlock_shared()** | `mutex.unlock_shared()` | `evl_unlock_read(&rwlock)` | |
| **ConditionVariable** | `std::condition_variable` | `struct evl_event` + `evl_new_event()` | Paired with evl_mutex |
| **CV::wait()** | `cv.wait(lock)` | `evl_wait_event(&evt, &mutex)` | |
| **CV::notify_one()** | `cv.notify_one()` | `evl_signal_event(&evt)` | Must hold paired mutex |
| **CV::notify_all()** | `cv.notify_all()` | `evl_broadcast_event(&evt)` | |
| **Time::now()** | `steady_clock::now()` | `evl_read_clock(EVL_CLOCK_MONOTONIC, &ts)` | Convert to ns |
| **Time::sleep()** | `this_thread::sleep_for()` | `evl_usleep()` / `evl_sleep_until()` | Absolute preferred |
| **ThreadPriority** | `pthread_setschedparam` | `evl_set_schedattr(efd, &attrs)` | Post-attach only |

---

## Implementation Notes

### Thread Lifecycle in EVL Backend

```cpp
// EVL Thread::start() implementation sketch
template<typename Func>
void start(Func&& func) {
    // 1. Create regular POSIX thread
    pthread_create(&posix_thread_, nullptr, [](void* arg) -> void* {
        auto* ctx = static_cast<ThreadContext*>(arg);

        // 2. Attach to EVL core
        int efd = evl_attach_self("commrat-%s:%d", ctx->config.name.c_str(), getpid());
        if (efd < 0) { /* handle error */ }

        // 3. Set scheduling attributes
        if (ctx->config.priority != ThreadPriority::IDLE) {
            struct evl_sched_attrs attrs;
            attrs.sched_policy = SCHED_FIFO;
            attrs.sched_priority = static_cast<int>(ctx->config.priority);
            evl_set_schedattr(efd, &attrs);
        }

        // 4. Enable health monitoring (detect accidental in-band switches)
        evl_set_thread_mode(efd, EVL_T_WOSS | EVL_T_HMSIG, nullptr);

        // 5. Set CPU affinity (if specified) - this is an in-band call,
        //    do it BEFORE entering the RT loop
        if (ctx->config.cpu_affinity >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(ctx->config.cpu_affinity, &cpuset);
            sched_setaffinity(0, sizeof(cpuset), &cpuset);
        }

        // 6. Run user function (RT loop)
        ctx->func();

        // 7. Cleanup - detach from EVL
        evl_detach_self();
        return nullptr;
    }, context);
}
```

### Mutex Lifecycle in EVL Backend

```cpp
class Mutex {
    struct evl_mutex evl_mutex_;
    bool initialized_ = false;

public:
    Mutex() {
        // Lazy init: EVL mutex creation requires evl_init() to have been called
        // Use static initializer for deferred creation
    }

    void init(const char* name) {
        int ret = evl_new_mutex(&evl_mutex_, "commrat-mtx-%s:%d", name, getpid());
        if (ret < 0) { /* handle error */ }
        initialized_ = true;
    }

    void lock() {
        if (!initialized_) init("unnamed");
        int ret = evl_lock_mutex(&evl_mutex_);
        // ret == 0 on success
    }

    bool try_lock() {
        if (!initialized_) init("unnamed");
        return evl_trylock_mutex(&evl_mutex_) == 0;
    }

    void unlock() {
        evl_unlock_mutex(&evl_mutex_);
    }

    ~Mutex() {
        if (initialized_) evl_close_mutex(&evl_mutex_);
    }
};
```

### Clock Implementation in EVL Backend

```cpp
class Time {
public:
    static Timestamp now() noexcept {
        struct timespec ts;
        evl_read_clock(EVL_CLOCK_MONOTONIC, &ts);
        return static_cast<Timestamp>(ts.tv_sec) * 1'000'000'000ULL +
               static_cast<Timestamp>(ts.tv_nsec);
    }

    static void sleep_ns(Timestamp ns) noexcept {
        if (ns <= 1'000'000'000ULL) {
            evl_usleep(static_cast<useconds_t>(ns / 1000));
        } else {
            struct timespec wake;
            evl_read_clock(EVL_CLOCK_MONOTONIC, &wake);
            wake.tv_nsec += ns % 1'000'000'000ULL;
            wake.tv_sec += ns / 1'000'000'000ULL;
            if (wake.tv_nsec >= 1'000'000'000L) {
                wake.tv_sec++;
                wake.tv_nsec -= 1'000'000'000L;
            }
            evl_sleep_until(EVL_CLOCK_MONOTONIC, &wake);
        }
    }
};
```

### Periodic Loop Pattern (EVL)

For timer-driven modules, use absolute sleep for jitter-free periodicity:

```cpp
void periodic_loop(Duration period) {
    struct timespec next_wake;
    evl_read_clock(EVL_CLOCK_MONOTONIC, &next_wake);

    while (!should_stop_) {
        // Advance to next period
        uint64_t period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(period).count();
        next_wake.tv_nsec += period_ns;
        while (next_wake.tv_nsec >= 1'000'000'000L) {
            next_wake.tv_sec++;
            next_wake.tv_nsec -= 1'000'000'000L;
        }

        evl_sleep_until(EVL_CLOCK_MONOTONIC, &next_wake);
        process();
    }
}
```

### What is FORBIDDEN in OOB Context

These will trigger automatic demotion to in-band (losing RT guarantees):

- `malloc()`, `free()`, `new`, `delete`
- `std::vector::push_back()`, any allocating STL operation
- `std::cout`, `printf()`, `fprintf()` (use `evl_printf()` instead)
- `open()`, `read()`, `write()` on regular files
- `pthread_create()`, `pthread_mutex_lock()` (POSIX mutexes, NOT EVL)
- `sched_setaffinity()` (do before entering RT loop)
- `throw` exceptions
- Any glibc function that may call a Linux syscall internally

### What IS Safe in OOB Context

- All `evl_*` functions
- `memcpy()`, `memmove()`, `memset()`, `memcmp()`
- `strcpy()`, `strlen()`, `strcmp()` etc.
- `std::atomic<T>` operations
- Stack-local arithmetic, array access
- `constexpr` evaluated code
- `sertial::fixed_vector`, `sertial::RingBuffer`

### Build Integration (CMake)

```cmake
option(COMMRAT_PLATFORM "Platform backend: std or evl" "std")

if(COMMRAT_PLATFORM STREQUAL "evl")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(EVL REQUIRED evl)

    target_compile_definitions(commrat PUBLIC COMMRAT_PLATFORM_EVL)
    target_include_directories(commrat PUBLIC ${EVL_INCLUDE_DIRS})
    target_link_libraries(commrat PUBLIC ${EVL_LIBRARIES} pthread)
else()
    target_compile_definitions(commrat PUBLIC COMMRAT_PLATFORM_STD)
    target_link_libraries(commrat PUBLIC pthread)
endif()
```

### Header Organization

```
include/commrat/platform/
    platform.hpp              # Includes correct backend via #ifdef
    threading.hpp             # Current monolithic (to be split)
    timestamp.hpp             # Current monolithic (to be split)
    std/
        thread.hpp            # std::thread wrapper
        mutex.hpp             # std::mutex wrapper
        shared_mutex.hpp      # std::shared_mutex wrapper
        condition_variable.hpp # std::condition_variable wrapper
        clock.hpp             # std::chrono wrapper
    evl/
        thread.hpp            # EVL thread (pthread + evl_attach)
        mutex.hpp             # evl_mutex wrapper
        shared_mutex.hpp      # evl_rwlock wrapper
        condition_variable.hpp # evl_event wrapper
        clock.hpp             # evl_read_clock wrapper
        init.hpp              # evl_init() lifecycle
```

---

## References

- EVL User API: https://v4.xenomai.org/core/user-api/index.html
- EVL Function Index: https://v4.xenomai.org/core/user-api/function_index/index.html
- EVL Thread: https://v4.xenomai.org/core/user-api/thread/index.html
- EVL Mutex: https://v4.xenomai.org/core/user-api/mutex/index.html
- EVL RWLock: https://v4.xenomai.org/core/user-api/rwlock/index.html
- EVL Event: https://v4.xenomai.org/core/user-api/event/index.html
- EVL Clock: https://v4.xenomai.org/core/user-api/clock/index.html
- EVL Scheduling: https://v4.xenomai.org/core/user-api/scheduling/index.html
- EVL Init: https://v4.xenomai.org/core/user-api/init/index.html
- CommRaT Platform Abstraction Design: docs/work/PLATFORM_ABSTRACTION_LAYER.md
