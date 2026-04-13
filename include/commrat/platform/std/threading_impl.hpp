/**
 * @file std/threading_impl.hpp
 * @brief Standard C++ threading backend for CommRaT
 * 
 * Implements Thread, Mutex, SharedMutex, ConditionVariable using
 * std::thread, std::mutex, std::shared_mutex, std::condition_variable_any.
 * 
 * This file is included by commrat/platform/threading.hpp when
 * COMMRAT_PLATFORM_STD is defined (default).
 * 
 * Not intended for direct inclusion by user code.
 */

#pragma once

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <cstdint>

// POSIX threading (Linux-specific features: naming, priority, affinity)
#include <pthread.h>
#include <sched.h>

namespace commrat {

// ============================================================================
// Thread (std::thread wrapper)
// ============================================================================

/**
 * @brief Thread wrapper with priority and affinity support
 * 
 * Wraps std::thread with additional features:
 * - Thread naming (pthread_setname_np)
 * - Priority / scheduling policy (SCHED_FIFO/RR)
 * - CPU affinity (pthread_setaffinity_np)
 * 
 * Usage:
 *   Thread worker(config, []{ do_work(); });
 *   worker.join();
 */
class Thread {
public:
    Thread() : config_{} {}
    
    template<typename Func>
    explicit Thread(Func&& func)
        : Thread(ThreadConfig{}, std::forward<Func>(func)) {
    }
    
    template<typename Func>
    Thread(const ThreadConfig& config, Func&& func)
        : config_(config), 
          thread_([cfg = config, f = std::forward<Func>(func)]() mutable {
              apply_config(cfg);
              f();
          }) {
    }
    
    explicit Thread(const ThreadConfig& config) 
        : config_(config) {
    }
    
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
    
    template<typename Func>
    void start(Func&& func) {
        if (thread_.joinable()) {
            return;
        }
        auto cfg = config_;
        thread_ = std::thread([cfg, f = std::forward<Func>(func)]() mutable {
            apply_config(cfg);
            f();
        });
    }
    
    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    void detach() {
        if (thread_.joinable()) {
            thread_.detach();
        }
    }
    
    bool joinable() const noexcept { return thread_.joinable(); }
    
    auto native_handle() { return thread_.native_handle(); }
    
    auto get_id() const noexcept { return thread_.get_id(); }
    
    const ThreadConfig& config() const noexcept { return config_; }
    
private:
    static void apply_config(const ThreadConfig& config) {
        pthread_t handle = pthread_self();
        
        // Set thread name (Linux, max 16 chars including null)
#ifdef __linux__
        if (!config.name.empty()) {
            pthread_setname_np(handle, config.name.substr(0, 15).c_str());
        }
#endif
        
        // Set scheduling policy and priority
        if (config.policy != SchedulingPolicy::NORMAL || 
            config.priority != ThreadPriority::NORMAL) {
            
            int policy = SCHED_OTHER;
            switch (config.policy) {
                case SchedulingPolicy::FIFO:        policy = SCHED_FIFO; break;
                case SchedulingPolicy::ROUND_ROBIN: policy = SCHED_RR;   break;
                default:                            policy = SCHED_OTHER; break;
            }
            
            struct sched_param param{};
            param.sched_priority = static_cast<int>(config.priority);
            pthread_setschedparam(handle, policy, &param);
        }
        
        // Set CPU affinity
        if (config.cpu_affinity >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(config.cpu_affinity, &cpuset);
            pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
        }
    }
    
    ThreadConfig config_;
    std::thread thread_;
};

// ============================================================================
// Mutex (std::mutex wrapper)
// ============================================================================

/**
 * @brief Mutex abstraction (std::mutex backend)
 * 
 * Future EVL backend: evl_mutex with priority inheritance.
 */
class Mutex {
public:
    Mutex() = default;
    ~Mutex() = default;
    
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    
    void lock() { mutex_.lock(); }
    bool try_lock() { return mutex_.try_lock(); }
    void unlock() { mutex_.unlock(); }

private:
    std::mutex mutex_;
};

// ============================================================================
// SharedMutex (std::shared_mutex wrapper)
// ============================================================================

/**
 * @brief Reader-writer lock (std::shared_mutex backend)
 * 
 * Multiple readers OR single writer.
 * Future EVL backend: evl_rwlock (writer-biased, no PI).
 */
class SharedMutex {
public:
    SharedMutex() = default;
    ~SharedMutex() = default;
    
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;
    
    void lock() { mutex_.lock(); }
    void lock_shared() { mutex_.lock_shared(); }
    bool try_lock() { return mutex_.try_lock(); }
    bool try_lock_shared() { return mutex_.try_lock_shared(); }
    void unlock() { mutex_.unlock(); }
    void unlock_shared() { mutex_.unlock_shared(); }

private:
    std::shared_mutex mutex_;
};

// ============================================================================
// ConditionVariable (std::condition_variable_any wrapper)
// ============================================================================

/**
 * @brief Condition variable abstraction
 * 
 * Uses std::condition_variable_any to work with CommRaT's Mutex wrapper
 * (accepts any lockable type, not just std::unique_lock<std::mutex>).
 * 
 * Future EVL backend: evl_event paired with evl_mutex.
 */
class ConditionVariable {
public:
    ConditionVariable() = default;
    ~ConditionVariable() = default;
    
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    
    void notify_one() noexcept { cv_.notify_one(); }
    void notify_all() noexcept { cv_.notify_all(); }
    
    void wait(std::unique_lock<Mutex>& lock) {
        cv_.wait(lock);
    }
    
    template<typename Predicate>
    void wait(std::unique_lock<Mutex>& lock, Predicate pred) {
        cv_.wait(lock, pred);
    }
    
    CvStatus wait_for(std::unique_lock<Mutex>& lock, Duration timeout) {
        auto status = cv_.wait_for(lock, timeout.to_chrono_ns());
        return (status == std::cv_status::timeout) 
            ? CvStatus::TIMEOUT 
            : CvStatus::NO_TIMEOUT;
    }
    
    template<typename Predicate>
    bool wait_for(std::unique_lock<Mutex>& lock, Duration timeout, Predicate pred) {
        return cv_.wait_for(lock, timeout.to_chrono_ns(), pred);
    }
    
private:
    std::condition_variable_any cv_;
};

} // namespace commrat
