/**
 * @file std/timestamp_impl.hpp
 * @brief Standard C++ clock/time backend for CommRaT
 * 
 * Implements the Time utility class using std::chrono clocks
 * and std::this_thread::sleep_for.
 * 
 * This file is included by commrat/platform/timestamp.hpp when
 * COMMRAT_PLATFORM_STD is defined (default).
 * 
 * Not intended for direct inclusion by user code.
 */

#pragma once

#include <chrono>
#include <thread>
#include <ctime>

namespace commrat {

/**
 * @brief Time utility class (std::chrono backend)
 * 
 * Provides unified interface for timestamps, sleeping, and conversions.
 * Future EVL backend uses evl_read_clock / evl_sleep_until.
 */
class Time {
public:
    /**
     * @brief Clock source types
     */
    enum class ClockSource {
        SYSTEM_CLOCK,      ///< std::chrono::system_clock (wall time)
        STEADY_CLOCK,      ///< std::chrono::steady_clock (monotonic, default)
        HIGH_RES_CLOCK,    ///< std::chrono::high_resolution_clock
        REALTIME_CLOCK,    ///< CLOCK_REALTIME (future: PTP, NTP sync)
        MONOTONIC_CLOCK    ///< CLOCK_MONOTONIC (POSIX monotonic)
    };
    
    // ========================================================================
    // Timestamp Access
    // ========================================================================
    
    /**
     * @brief Get current timestamp in nanoseconds (default clock)
     * @return Current time as uint64_t nanoseconds
     * 
     * Real-time safe: Yes (if using MONOTONIC_CLOCK)
     */
    static Timestamp now() noexcept {
        return get_timestamp(current_clock_source_);
    }
    
    /**
     * @brief Get timestamp from specific clock source
     */
    static Timestamp get_timestamp(ClockSource source = ClockSource::STEADY_CLOCK) noexcept {
        switch (source) {
            case ClockSource::SYSTEM_CLOCK:
                return system_clock_now();
            case ClockSource::STEADY_CLOCK:
            case ClockSource::HIGH_RES_CLOCK:
                return steady_clock_now();
            case ClockSource::REALTIME_CLOCK:
                return posix_clock_now(CLOCK_REALTIME);
            case ClockSource::MONOTONIC_CLOCK:
                return posix_clock_now(CLOCK_MONOTONIC);
            default:
                return steady_clock_now();
        }
    }
    
    /**
     * @brief Set default clock source for future now() calls
     * @note Not thread-safe - call once at initialization
     */
    static void set_clock_source(ClockSource source) noexcept {
        current_clock_source_ = source;
    }
    
    // ========================================================================
    // Conversion Utilities
    // ========================================================================
    
    /**
     * @brief Convert Duration to nanoseconds (Timestamp)
     */
    static constexpr Timestamp to_nanoseconds(Duration duration) noexcept {
        return static_cast<Timestamp>(duration.count_ns());
    }
    
    /**
     * @brief Convert nanoseconds to Duration
     */
    static constexpr Duration from_nanoseconds(Timestamp ns) noexcept {
        return Duration::nanoseconds(static_cast<int64_t>(ns));
    }
    
    /**
     * @brief Convert milliseconds count to nanoseconds
     */
    static constexpr Timestamp milliseconds_to_ns(uint64_t ms) noexcept {
        return ms * 1'000'000;
    }
    
    /**
     * @brief Convert microseconds count to nanoseconds
     */
    static constexpr Timestamp microseconds_to_ns(uint64_t us) noexcept {
        return us * 1'000;
    }
    
    /**
     * @brief Convert nanoseconds to milliseconds count
     */
    static constexpr uint64_t ns_to_milliseconds(Timestamp ns) noexcept {
        return ns / 1'000'000;
    }
    
    /**
     * @brief Convert nanoseconds to microseconds count
     */
    static constexpr uint64_t ns_to_microseconds(Timestamp ns) noexcept {
        return ns / 1'000;
    }
    
    // ========================================================================
    // Timestamp Arithmetic
    // ========================================================================
    
    /**
     * @brief Absolute time difference between two timestamps
     */
    static constexpr Timestamp diff(Timestamp t1, Timestamp t2) noexcept {
        return (t1 > t2) ? (t1 - t2) : (t2 - t1);
    }
    
    /**
     * @brief Check if timestamp is within tolerance of target
     */
    static constexpr bool is_within_tolerance(Timestamp timestamp, 
                                             Timestamp target,
                                             Timestamp tolerance_ns) noexcept {
        return diff(timestamp, target) <= tolerance_ns;
    }
    
    // ========================================================================
    // Sleep
    // ========================================================================
    
    /**
     * @brief Sleep for specified Duration
     * @param duration Time to sleep
     * 
     * EVL backend: uses evl_usleep() for OOB-safe sleep
     */
    static void sleep(Duration duration) noexcept {
        std::this_thread::sleep_for(duration.to_chrono_ns());
    }
    
    /**
     * @brief Sleep for specified nanoseconds
     */
    static void sleep_ns(Timestamp ns) noexcept {
        std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
    }
    
    /**
     * @brief Sleep until absolute timestamp
     * @param target Target timestamp in nanoseconds
     * 
     * EVL backend: uses evl_sleep_until() for jitter-free periodic timing
     */
    static void sleep_until(Timestamp target) noexcept {
        Timestamp now_ts = now();
        if (target > now_ts) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(target - now_ts));
        }
    }
    
    /**
     * @brief Yield current thread's timeslice
     * 
     * EVL backend: uses sched_yield() in OOB context
     */
    static void yield() noexcept {
        std::this_thread::yield();
    }

private:
    static Timestamp system_clock_now() noexcept {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
    
    static Timestamp steady_clock_now() noexcept {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
    
    static Timestamp posix_clock_now(clockid_t clock_id) noexcept {
        struct timespec ts;
        if (clock_gettime(clock_id, &ts) == 0) {
            return static_cast<Timestamp>(ts.tv_sec) * 1'000'000'000 + 
                   static_cast<Timestamp>(ts.tv_nsec);
        }
        return steady_clock_now();
    }
    
    static inline ClockSource current_clock_source_ = ClockSource::STEADY_CLOCK;
};

} // namespace commrat
