#pragma once

#include <cassert>
#include <type_traits>

namespace commrat {

/**
 * @file synced.hpp
 * @brief Synced<T> - Zero-copy wrapper for synchronized secondary input data
 * 
 * Wraps a const T* reference with validity/freshness metadata.
 * Forces correct handling of stale/invalid data without runtime errors.
 * 
 * Design:
 * - Zero-copy: Holds const T* pointer (reference to input's storage)
 * - Safe: Validity flag prevents invalid access
 * - Ergonomic: Clear API for fresh vs stale vs invalid data
 * - No exceptions: All error handling explicit via API
 * 
 * States:
 * - **Fresh**: get_data() found new data at requested timestamp
 * - **Stale**: Valid but not fresh (cached old data, still within tolerance)
 * - **Invalid**: get_data() failed (no data, timeout, out of tolerance)
 * 
 * User Access (const Synced<T>&):
 * @code
 * void process(const IMUData& imu, const Synced<GPSData>& gps, FusedData& output) {
 *     if (gps) {
 *         // Fresh GPS data available
 *         output = fuse_with_gps(imu, gps.value());
 *     } else if (gps.has_stale()) {
 *         // Stale GPS data (old but valid)
 *         output = fuse_with_gps(imu, gps.stale());
 *     } else {
 *         // No GPS data - dead reckoning
 *         output = dead_reckoning(imu);
 *     }
 *     
 *     // Or use helpers for defaults
 *     const GPSData& data = gps.value_or(default_gps);  // Fresh or default
 *     const GPSData& data = gps.stale_or(default_gps);  // Valid or default
 *     const GPSData& data = *gps;                       // Valid (most permissive)
 * }
 * @endcode
 * 
 * SyncedInput Internal Use (non-const):
 * @code
 * synced = gps_message.payload;  // Set fresh data
 * synced.mark_stale();           // Mark as stale (keep data)
 * synced.reset();                // Mark invalid
 * @endcode
 */
template<typename T>
class Synced {
public:
    /**
     * @brief Construct invalid Synced (no data)
     */
    constexpr Synced() noexcept
        : data_(nullptr)
        , is_valid_(false)
        , is_fresh_(false)
    {}
    
    /**
     * @brief Construct from reference with freshness
     * @param data Reference to synchronized data
     * @param is_fresh True if data is newly received, false if cached/stale
     */
    constexpr Synced(const T& data, bool is_fresh) noexcept
        : data_(&data)
        , is_valid_(true)
        , is_fresh_(is_fresh)
    {}
    
    // ========================================================================
    // Validity Checks (const - user access)
    // ========================================================================
    
    /**
     * @brief Check if data is FRESH (strict check)
     * 
     * Use this for if-checks when you only want fresh data.
     * @return true only if data is valid AND fresh (newly received)
     */
    constexpr explicit operator bool() const noexcept {
        return is_fresh_;
    }
    
    /**
     * @brief Check if data is fresh
     * @return true if data is valid AND fresh (not cached)
     */
    constexpr bool is_fresh() const noexcept {
        return is_fresh_;
    }
    
    /**
     * @brief Check if data is stale (valid but not fresh)
     * @return true if data is valid BUT not fresh (old cached data)
     */
    constexpr bool has_stale() const noexcept {
        return is_valid_ && !is_fresh_;
    }
    
    /**
     * @brief Check if any valid data exists (fresh or stale)
     * @return true if get_data() succeeded (has data, may be stale)
     */
    constexpr bool is_valid() const noexcept {
        return is_valid_;
    }
    
    // ========================================================================
    // Data Access (const - user access, zero-copy)
    // ========================================================================
    
    /**
     * @brief Get FRESH data reference (strict)
     * 
     * @return const T& reference to fresh synchronized data
     * @pre is_fresh() must be true (asserted)
     * 
     * @note Use this when you need fresh data only.
     *       For any valid data (fresh or stale), use stale() instead.
     */
    constexpr const T& value() const noexcept {
        assert(is_fresh_ && "Synced::value() requires fresh data - use stale() for any valid data");
        return *data_;
    }
    
    /**
     * @brief Get ANY VALID data reference (fresh or stale)
     * 
     * @return const T& reference to synchronized data (may be old)
     * @pre is_valid() must be true (asserted)
     * 
     * @note Most permissive accessor - accepts stale data.
     *       Use this when any data (even old) is acceptable.
     */
    constexpr const T& stale() const noexcept {
        assert(is_valid_ && "Synced::stale() requires valid data - check is_valid() first");
        return *data_;
    }
    
    /**
     * @brief Get fresh data or default
     * 
     * @param default_value Fallback if data not fresh
     * @return const T& reference to fresh data if available, otherwise default_value
     */
    constexpr const T& value_or(const T& default_value) const noexcept {
        return is_fresh_ ? *data_ : default_value;
    }
    
    /**
     * @brief Get valid data (fresh or stale) or default
     * 
     * @param default_value Fallback if data invalid
     * @return const T& reference to valid data if available, otherwise default_value
     */
    constexpr const T& stale_or(const T& default_value) const noexcept {
        return is_valid_ ? *data_ : default_value;
    }
    
    /**
     * @brief Dereference operator - most permissive (same as stale())
     * 
     * @return const T& reference to valid data (fresh or stale)
     * @pre is_valid() must be true
     */
    constexpr const T& operator*() const noexcept {
        return stale();
    }
    
    /**
     * @brief Arrow operator - most permissive (same as stale())
     * 
     * Allows direct member access: synced->member
     * 
     * @return const T* pointer to valid data (fresh or stale)
     * @pre is_valid() must be true (asserted)
     */
    constexpr const T* operator->() const noexcept {
        assert(is_valid_ && "Synced::operator->() requires valid data - check is_valid() first");
        return data_;
    }
    
    // ========================================================================
    // Modification (non-const - SyncedInput internal use only)
    // ========================================================================
    
    /**
     * @brief Set fresh data (assignment operator)
     * 
     * Marks data as valid and fresh.
     * Used by SyncedInput when get_data() succeeds with new data.
     * 
     * @param data Reference to new data
     * @return Reference to this
     */
    constexpr Synced& operator=(const T& data) noexcept {
        data_ = &data;
        is_valid_ = true;
        is_fresh_ = true;
        return *this;
    }
    
    /**
     * @brief Mark current data as stale (not fresh anymore)
     * 
     * Keeps data pointer but marks as not fresh.
     * Used when time passes and data ages.
     */
    constexpr void mark_stale() noexcept {
        is_fresh_ = false;
    }
    
    /**
     * @brief Reset to invalid state (no data)
     * 
     * Clears data pointer and marks as invalid.
     * Equivalent to assigning a default-constructed Synced.
     */
    constexpr void reset() noexcept {
        data_ = nullptr;
        is_valid_ = false;
        is_fresh_ = false;
    }
    
private:
    const T* data_;      ///< Pointer to synchronized data (nullptr if invalid)
    bool is_valid_;      ///< True if get_data() succeeded
    bool is_fresh_;      ///< True if data is newly received (not cached)
};

} // namespace commrat
