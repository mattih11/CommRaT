/**
 * @file timestamp.hpp
 * @brief Unified timestamp and time utility abstractions for CommRaT
 * 
 * Provides a clean abstraction layer for:
 * - Getting current time (system clock or realtime clock)
 * - Converting between time units
 * - Timestamp comparisons and arithmetic
 * 
 * Future: Can be switched to realtime clock sources (CLOCK_MONOTONIC, PTP, etc.)
 * without changing user code.
 */

#pragma once

#include <chrono>
#include <thread>  // For std::this_thread::sleep_for
#include <cstdint>
#include <ctime>

namespace commrat {

/**
 * @brief Timestamp type - uint64_t nanoseconds since epoch
 * 
 * Compatible with TiMS timestamp format.
 * Range: ~584 years from epoch (sufficient for all practical purposes)
 */
using Timestamp = uint64_t;

/**
 * @brief Duration type aliases (compatible with std::chrono but using uint64_t)
 * 
 * These provide drop-in replacements for std::chrono::duration types
 * while maintaining compatibility with future realtime clock sources.
 */
using Nanoseconds = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;
using Minutes = std::chrono::minutes;
using Hours = std::chrono::hours;

/**
 * @brief Time utility class - abstraction over clock sources
 * 
 * Provides unified interface for getting timestamps and converting units.
 * Can be configured to use different clock sources (system, monotonic, realtime).
 * 
 * Thread-safe: All methods are const and stateless.
 */
class Time {
public:
    /**
     * @brief Clock source types
     */
    enum class ClockSource {
        SYSTEM_CLOCK,      ///< std::chrono::system_clock (wall time)
        STEADY_CLOCK,      ///< std::chrono::steady_clock (monotonic)
        HIGH_RES_CLOCK,    ///< std::chrono::high_resolution_clock
        REALTIME_CLOCK,    ///< CLOCK_REALTIME (future: PTP, NTP sync)
        MONOTONIC_CLOCK    ///< CLOCK_MONOTONIC (future: realtime monotonic)
    };
    
    /**
     * @brief Get current timestamp in nanoseconds
     * 
     * @return Current time as uint64_t nanoseconds
     * 
     * Real-time safe: Yes (if using MONOTONIC_CLOCK)
     */
    static Timestamp now() noexcept {
        return get_timestamp(current_clock_source_);
    }
    
    /**
     * @brief Get current timestamp from specific clock source
     * 
     * @param source Clock source to use
     * @return Timestamp in nanoseconds
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
     * @brief Set default clock source for all future now() calls
     * 
     * @param source Clock source to use
     * 
     * Not thread-safe: Should be called once at initialization
     */
    static void set_clock_source(ClockSource source) noexcept {
        current_clock_source_ = source;
    }
    
    /**
     * @brief Convert std::chrono::duration to nanoseconds
     */
    template<typename Rep, typename Period>
    static constexpr Timestamp to_nanoseconds(std::chrono::duration<Rep, Period> duration) noexcept {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()
        );
    }
    
    /**
     * @brief Convert nanoseconds to std::chrono::duration
     */
    template<typename Duration>
    static constexpr Duration from_nanoseconds(Timestamp ns) noexcept {
        return std::chrono::duration_cast<Duration>(std::chrono::nanoseconds(ns));
    }
    
    /**
     * @brief Convert milliseconds to nanoseconds
     */
    static constexpr Timestamp milliseconds_to_ns(uint64_t ms) noexcept {
        return ms * 1'000'000;
    }
    
    /**
     * @brief Convert microseconds to nanoseconds
     */
    static constexpr Timestamp microseconds_to_ns(uint64_t us) noexcept {
        return us * 1'000;
    }
    
    /**
     * @brief Convert nanoseconds to milliseconds
     */
    static constexpr uint64_t ns_to_milliseconds(Timestamp ns) noexcept {
        return ns / 1'000'000;
    }
    
    /**
     * @brief Convert nanoseconds to microseconds
     */
    static constexpr uint64_t ns_to_microseconds(Timestamp ns) noexcept {
        return ns / 1'000;
    }
    
    /**
     * @brief Calculate absolute time difference between two timestamps
     * 
     * @param t1 First timestamp
     * @param t2 Second timestamp
     * @return Absolute difference in nanoseconds
     */
    static constexpr Timestamp diff(Timestamp t1, Timestamp t2) noexcept {
        return (t1 > t2) ? (t1 - t2) : (t2 - t1);
    }
    
    /**
     * @brief Check if timestamp is within tolerance of target
     * 
     * @param timestamp Timestamp to check
     * @param target Target timestamp
     * @param tolerance_ns Tolerance in nanoseconds
     * @return true if |timestamp - target| <= tolerance
     */
    static constexpr bool is_within_tolerance(Timestamp timestamp, 
                                             Timestamp target,
                                             Timestamp tolerance_ns) noexcept {
        return diff(timestamp, target) <= tolerance_ns;
    }
    
    /**
     * @brief Sleep for specified nanoseconds
     * 
     * Uses high-resolution sleep if available.
     * 
     * @param ns Nanoseconds to sleep
     * 
     * Real-time safe: Depends on OS scheduler
     */
    static void sleep_ns(Timestamp ns) noexcept {
        std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
    }
    
    /**
     * @brief Sleep for specified duration
     */
    template<typename Rep, typename Period>
    static void sleep(std::chrono::duration<Rep, Period> duration) noexcept {
        std::this_thread::sleep_for(duration);
    }

private:
    // Implementation helpers
    static Timestamp system_clock_now() noexcept {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    
    static Timestamp steady_clock_now() noexcept {
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    
    static Timestamp posix_clock_now(clockid_t clock_id) noexcept {
        struct timespec ts;
        if (clock_gettime(clock_id, &ts) == 0) {
            return static_cast<Timestamp>(ts.tv_sec) * 1'000'000'000 + 
                   static_cast<Timestamp>(ts.tv_nsec);
        }
        // Fallback to steady clock on error
        return steady_clock_now();
    }
    
    // Default clock source (can be changed via set_clock_source)
    static inline ClockSource current_clock_source_ = ClockSource::STEADY_CLOCK;
};

/**
 * @brief Timestamp comparison and arithmetic operators
 * 
 * NOTE: These operators are NOT NEEDED since Timestamp is just uint64_t.
 * They are commented out to avoid C++ compilation errors (operator overloading
 * for primitive types is not allowed). Use normal uint64_t operators directly.
 */

/*
inline constexpr bool operator<(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs < rhs;
}

inline constexpr bool operator<=(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs <= rhs;
}

inline constexpr bool operator>(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs > rhs;
}

inline constexpr bool operator>=(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs >= rhs;
}

inline constexpr Timestamp operator+(Timestamp ts, uint64_t ns) noexcept {
    return ts + ns;
}

inline constexpr Timestamp operator-(Timestamp ts, uint64_t ns) noexcept {
    return ts - ns;
}

inline constexpr Timestamp operator-(Timestamp t1, Timestamp t2) noexcept {
    return t1 - t2;
}
*/

/**
 * @brief Convenience literals for time durations
 * 
 * Usage:
 *   using namespace commrat::literals;
 *   auto timeout = 100_ms;
 *   auto delay = 50_us;
 */
namespace literals {
    constexpr Timestamp operator""_ns(unsigned long long ns) noexcept {
        return static_cast<Timestamp>(ns);
    }
    
    constexpr Timestamp operator""_us(unsigned long long us) noexcept {
        return Time::microseconds_to_ns(us);
    }
    
    constexpr Timestamp operator""_ms(unsigned long long ms) noexcept {
        return Time::milliseconds_to_ns(ms);
    }
    
    constexpr Timestamp operator""_s(unsigned long long s) noexcept {
        return Time::milliseconds_to_ns(s * 1000);
    }
} // namespace literals

} // namespace commrat
