/**
 * @file test_synced_wrapper.cpp
 * @brief Test Synced<T> wrapper API
 */

#include "commrat/module/io/synced.hpp"
#include <iostream>
#include <cassert>

using namespace commrat;

struct GPSData {
    double latitude;
    double longitude;
    float altitude;
};

int main() {
    std::cout << "Testing Synced<T> wrapper...\n";
    
    // Test 1: Default construction (invalid)
    {
        Synced<GPSData> s;
        assert(!s);
        assert(!s.is_fresh());
        assert(!s.has_stale());
        assert(!s.is_valid());
        std::cout << "  Default construction: PASS\n";
    }
    
    // Test 2: Fresh data
    {
        GPSData data{37.7749, -122.4194, 10.0f};
        Synced<GPSData> s(data, true);  // Fresh
        
        assert(s);                    // operator bool() checks freshness
        assert(s.is_fresh());
        assert(s.is_valid());
        
        const GPSData& val = s.value();  // Fresh accessor
        assert(val.latitude == 37.7749);
        
        const GPSData& stale_val = s.stale();  // Also works for fresh
        assert(stale_val.latitude == 37.7749);
        
        std::cout << "  Fresh data: PASS\n";
    }
    
    // Test 3: Stale data
    {
        GPSData data{37.7749, -122.4194, 10.0f};
        Synced<GPSData> s(data, false);  // Stale
        
        assert(!s);                   // operator bool() strict (fresh only)
        assert(!s.is_fresh());
        assert(s.has_stale());
        assert(s.is_valid());
        
        // value() would assert (expects fresh)
        const GPSData& stale_val = s.stale();
        assert(stale_val.latitude == 37.7749);
        
        std::cout << "  Stale data: PASS\n";
    }
    
    // Test 4: value_or / stale_or
    {
        GPSData default_gps{0.0, 0.0, 0.0f};
        
        // Fresh data
        GPSData fresh_data{37.7749, -122.4194, 10.0f};
        Synced<GPSData> s_fresh(fresh_data, true);
        
        const GPSData& v1 = s_fresh.value_or(default_gps);
        assert(v1.latitude == 37.7749);  // Uses fresh
        
        const GPSData& v2 = s_fresh.stale_or(default_gps);
        assert(v2.latitude == 37.7749);  // Uses fresh
        
        // Stale data
        GPSData stale_data{40.7128, -74.0060, 5.0f};
        Synced<GPSData> s_stale(stale_data, false);
        
        const GPSData& v3 = s_stale.value_or(default_gps);
        assert(v3.latitude == 0.0);  // Uses default (not fresh)
        
        const GPSData& v4 = s_stale.stale_or(default_gps);
        assert(v4.latitude == 40.7128);  // Uses stale
        
        // Invalid data
        Synced<GPSData> s_invalid;
        
        const GPSData& v5 = s_invalid.value_or(default_gps);
        assert(v5.latitude == 0.0);  // Uses default
        
        const GPSData& v6 = s_invalid.stale_or(default_gps);
        assert(v6.latitude == 0.0);  // Uses default
        
        std::cout << "  value_or / stale_or: PASS\n";
    }
    
    // Test 5: Mutation via operator= (SyncedInput use case)
    {
        Synced<GPSData> s;
        assert(!s.is_valid());
        
        GPSData fresh_data{37.7749, -122.4194, 10.0f};
        s = fresh_data;  // Set fresh
        
        assert(s.is_fresh());
        assert(s.is_valid());
        assert(s.value().latitude == 37.7749);
        
        s.mark_stale();
        assert(!s.is_fresh());
        assert(s.has_stale());
        assert(s.stale().latitude == 37.7749);
        
        s.reset();
        assert(!s.is_valid());
        
        std::cout << "  Mutation (operator=, mark_stale, reset): PASS\n";
    }
    
    // Test 6: operator* (permissive dereference)
    {
        GPSData data{37.7749, -122.4194, 10.0f};
        
        // Fresh data
        Synced<GPSData> s_fresh(data, true);
        const GPSData& deref = *s_fresh;
        assert(deref.latitude == 37.7749);
        
        // Stale data
        Synced<GPSData> s_stale(data, false);
        const GPSData& deref_stale = *s_stale;
        assert(deref_stale.latitude == 37.7749);
        
        std::cout << "  operator* (dereference): PASS\n";
    }
    
    std::cout << "\nAll tests PASSED!\n";
    return 0;
}
