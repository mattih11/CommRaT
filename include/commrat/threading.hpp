/**
 * @file threading.hpp
 * @brief Unified threading and synchronization abstractions for CommRaT
 * 
 * Provides clean abstractions for:
 * - Thread creation and management
 * - Mutexes and locks (with future realtime support)
 * - Thread priorities and affinity
 * 
 * Future: Can be switched to realtime thread APIs (pthread RT, SCHED_FIFO, etc.)
 * without changing user code.
 */

#pragma once

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <string>
#include <cstdint>

// For future realtime support
#include <pthread.h>
#include <sched.h>

namespace commrat {

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
 * @brief Thread wrapper with realtime support
 * 
 * Abstraction over std::thread with additional features:
 * - Thread priorities
 * - CPU affinity
 * - Realtime scheduling policies
 * - Named threads (for debugging)
 * 
 * Usage:
 *   Thread worker(config, []{ do_work(); });
 *   worker.join();
 */
class Thread {
public:
    /**
     * @brief Default constructor - no thread running
     */
    Thread() = default;
    
    /**
     * @brief Create and start thread with function
     * 
     * @param config Thread configuration
     * @param func Function to execute
     */
    template<typename Func>
    Thread(const ThreadConfig& config, Func&& func)
        : config_(config), 
          thread_([this, f = std::forward<Func>(func)]() mutable {
              this->thread_function(std::move(f));
          }) {
    }
    
    /**
     * @brief Create thread without starting (call start() later)
     */
    explicit Thread(const ThreadConfig& config) 
        : config_(config) {
    }
    
    /**
     * @brief Destructor - joins if joinable
     */
    ~Thread() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    // Non-copyable, movable
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&) = default;
    Thread& operator=(Thread&&) = default;
    
    /**
     * @brief Start thread with function (if not already started)
     */
    template<typename Func>
    void start(Func&& func) {
        if (thread_.joinable()) {
            return;  // Already running
        }
        thread_ = std::thread([this, f = std::forward<Func>(func)]() mutable {
            this->thread_function(std::move(f));
        });
    }
    
    /**
     * @brief Join thread (wait for completion)
     */
    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    /**
     * @brief Detach thread
     */
    void detach() {
        if (thread_.joinable()) {
            thread_.detach();
        }
    }
    
    /**
     * @brief Check if thread is joinable
     */
    bool joinable() const noexcept {
        return thread_.joinable();
    }
    
    /**
     * @brief Get native thread handle
     */
    std::thread::native_handle_type native_handle() {
        return thread_.native_handle();
    }
    
    /**
     * @brief Get thread ID
     */
    std::thread::id get_id() const noexcept {
        return thread_.get_id();
    }
    
    /**
     * @brief Get thread configuration
     */
    const ThreadConfig& config() const noexcept {
        return config_;
    }

private:
    /**
     * @brief Thread entry point - applies config and runs user function
     */
    template<typename Func>
    void thread_function(Func&& func) {
        // Set thread name (Linux-specific)
#ifdef __linux__
        if (!config_.name.empty()) {
            pthread_setname_np(pthread_self(), config_.name.c_str());
        }
#endif
        
        // Set priority and scheduling policy
        apply_thread_config();
        
        // Run user function
        func();
    }
    
    /**
     * @brief Apply thread configuration (priority, affinity, policy)
     */
    void apply_thread_config() {
        pthread_t thread_handle = pthread_self();
        
        // Set scheduling policy and priority
        if (config_.policy != SchedulingPolicy::NORMAL || 
            config_.priority != ThreadPriority::NORMAL) {
            
            int policy = SCHED_OTHER;
            switch (config_.policy) {
                case SchedulingPolicy::FIFO:
                    policy = SCHED_FIFO;
                    break;
                case SchedulingPolicy::ROUND_ROBIN:
                    policy = SCHED_RR;
                    break;
                default:
                    policy = SCHED_OTHER;
            }
            
            struct sched_param param;
            param.sched_priority = static_cast<int>(config_.priority);
            
            // Note: Requires CAP_SYS_NICE capability or root for SCHED_FIFO/RR
            pthread_setschedparam(thread_handle, policy, &param);
        }
        
        // Set CPU affinity
        if (config_.cpu_affinity >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(config_.cpu_affinity, &cpuset);
            pthread_setaffinity_np(thread_handle, sizeof(cpu_set_t), &cpuset);
        }
    }
    
    ThreadConfig config_;
    std::thread thread_;
};

/**
 * @brief Mutex wrapper (future: realtime mutex support)
 * 
 * Currently wraps std::mutex, future can switch to priority-inheriting mutex.
 */
class Mutex {
public:
    Mutex() = default;
    ~Mutex() = default;
    
    // Non-copyable, non-movable
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    
    void lock() { mutex_.lock(); }
    bool try_lock() { return mutex_.try_lock(); }
    void unlock() { mutex_.unlock(); }
    
    std::mutex& native() { return mutex_; }
    
private:
    std::mutex mutex_;
};

/**
 * @brief Shared mutex wrapper (reader-writer lock)
 * 
 * Multiple readers OR single writer.
 * Useful for ring buffers where reads are frequent, writes are rare.
 */
class SharedMutex {
public:
    SharedMutex() = default;
    ~SharedMutex() = default;
    
    // Non-copyable, non-movable
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;
    
    void lock() { mutex_.lock(); }               // Exclusive (write) lock
    void lock_shared() { mutex_.lock_shared(); } // Shared (read) lock
    bool try_lock() { return mutex_.try_lock(); }
    bool try_lock_shared() { return mutex_.try_lock_shared(); }
    void unlock() { mutex_.unlock(); }
    void unlock_shared() { mutex_.unlock_shared(); }
    
    std::shared_mutex& native() { return mutex_; }
    
private:
    std::shared_mutex mutex_;
};

/**
 * @brief Scoped lock guard (RAII)
 * 
 * Usage:
 *   Mutex mtx;
 *   {
 *     Lock lock(mtx);  // Acquires lock
 *     // ... critical section ...
 *   } // Releases lock automatically
 */
using Lock = std::lock_guard<Mutex>;
using UniqueLock = std::unique_lock<Mutex>;

/**
 * @brief Scoped shared lock (RAII) - for readers
 */
using SharedLock = std::shared_lock<SharedMutex>;

/**
 * @brief Scoped unique lock (RAII) - for writers
 */
using UniqueLockShared = std::unique_lock<SharedMutex>;

/**
 * @brief Condition variable wrapper
 */
class ConditionVariable {
public:
    ConditionVariable() = default;
    ~ConditionVariable() = default;
    
    // Non-copyable, non-movable
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    
    void notify_one() noexcept { cv_.notify_one(); }
    void notify_all() noexcept { cv_.notify_all(); }
    
    void wait(UniqueLock& lock) { cv_.wait(lock); }
    
    template<typename Predicate>
    void wait(UniqueLock& lock, Predicate pred) {
        cv_.wait(lock, pred);
    }
    
    template<typename Rep, typename Period>
    std::cv_status wait_for(UniqueLock& lock, 
                           const std::chrono::duration<Rep, Period>& rel_time) {
        return cv_.wait_for(lock, rel_time);
    }
    
private:
    std::condition_variable cv_;
};

/**
 * @brief Scoped synchronized block - convenience wrapper
 * 
 * Usage:
 *   Mutex mtx;
 *   Synchronized(mtx) {
 *     // Critical section
 *   }
 * 
 * Expands to: { Lock lock(mtx); ... }
 */
#define Synchronized(mutex) \
    if (commrat::Lock _lock_##__LINE__{mutex}; true)

/**
 * @brief Scoped read-locked block (multiple readers)
 * 
 * Usage:
 *   SharedMutex mtx;
 *   ReadLocked(mtx) {
 *     // Read-only access (multiple readers OK)
 *   }
 */
#define ReadLocked(mutex) \
    if (commrat::SharedLock _lock_##__LINE__{mutex}; true)

/**
 * @brief Scoped write-locked block (exclusive writer)
 * 
 * Usage:
 *   SharedMutex mtx;
 *   WriteLocked(mutex) {
 *     // Exclusive write access
 *   }
 */
#define WriteLocked(mutex) \
    if (commrat::UniqueLockShared _lock_##__LINE__{mutex}; true)

} // namespace commrat
