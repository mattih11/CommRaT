# Generic Algorithm Modules

**Status**: Concept Phase  
**Priority**: Low  
**Created**: February 12, 2026

## Overview

Provide reusable algorithm modules that work with any message type supporting required arithmetic operations. Similar to STL algorithms but for CommRaT modules.

## Motivation

Many signal processing operations are generic:
- Moving average
- Low-pass filter
- Kalman filter
- Windowed statistics (min, max, mean, stddev)
- Rate limiting
- Dead-band filtering

Currently, users must implement these for each message type. Generic modules eliminate duplication.

## Design

### Concept Requirements

```cpp
// Type must support arithmetic operations
template<typename T>
concept Arithmetic = requires(T a, T b, float scalar) {
    { a + b } -> std::convertible_to<T>;
    { a - b } -> std::convertible_to<T>;
    { a * scalar } -> std::convertible_to<T>;
    { a / scalar } -> std::convertible_to<T>;
};

template<typename T>
concept Comparable = requires(T a, T b) {
    { a < b } -> std::convertible_to<bool>;
    { a > b } -> std::convertible_to<bool>;
};

template<typename T>
concept Statistical = Arithmetic<T> && Comparable<T>;
```

### Example: Moving Average

```cpp
template<typename AppType, typename T>
    requires Arithmetic<T>
class MovingAverage : public AppType::template Module<
    Output<T>,
    Input<T>,
    Params<MovingAverageParams>
> {
    using Base = typename AppType::template Module<Output<T>, Input<T>, Params<MovingAverageParams>>;
    
    sertial::RingBuffer<T, 100> history_;
    
protected:
    struct MovingAverageParams : Parameters<
        Param<"window_size", int, Default<10>, Range<1, 100>>
    > {};
    
    T process(const T& input) override {
        int window = this->params_.template get<"window_size">();
        
        history_.push_back(input);
        
        // Generic accumulation
        T sum = T{};  // Zero initialization
        size_t count = std::min(history_.size(), static_cast<size_t>(window));
        
        for (size_t i = 0; i < count; ++i) {
            sum = sum + history_[history_.size() - 1 - i];
        }
        
        return sum / static_cast<float>(count);
    }
};

// Usage
struct Vector3 {
    float x, y, z;
    
    Vector3 operator+(const Vector3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }
    
    Vector3 operator/(float scalar) const {
        return {x / scalar, y / scalar, z / scalar};
    }
};

using IMUFilter = MovingAverage<MyApp, Vector3>;
```

### Example: Low-Pass Filter

```cpp
template<typename AppType, typename T>
    requires Arithmetic<T>
class LowPassFilter : public AppType::template Module<
    Output<T>,
    Input<T>,
    Params<LowPassParams>
> {
    std::optional<T> prev_output_;
    
protected:
    struct LowPassParams : Parameters<
        Param<"alpha", float, Default<0.1f>, Range<0.0f, 1.0f>>
    > {};
    
    T process(const T& input) override {
        float alpha = this->params_.template get<"alpha">();
        
        if (!prev_output_) {
            prev_output_ = input;
            return input;
        }
        
        // output = alpha * input + (1 - alpha) * prev_output
        T output = input * alpha + (*prev_output_) * (1.0f - alpha);
        prev_output_ = output;
        
        return output;
    }
};
```

### Example: Windowed Statistics

```cpp
template<typename AppType, typename T>
    requires Statistical<T>
class WindowedStats : public AppType::template Module<
    Outputs<
        Message::Data<StatisticsData<T>>  // min, max, mean, stddev
    >,
    Input<T>,
    Params<StatsParams>
> {
    sertial::RingBuffer<T, 1000> history_;
    
protected:
    struct StatsParams : Parameters<
        Param<"window_size", int, Default<100>, Range<1, 1000>>
    > {};
    
    struct StatisticsData {
        T min;
        T max;
        T mean;
        float stddev;
    };
    
    StatisticsData<T> process(const T& input) override {
        int window = this->params_.template get<"window_size">();
        
        history_.push_back(input);
        
        size_t count = std::min(history_.size(), static_cast<size_t>(window));
        
        // Find min/max
        T min_val = history_[history_.size() - 1];
        T max_val = history_[history_.size() - 1];
        T sum = T{};
        
        for (size_t i = 0; i < count; ++i) {
            const T& val = history_[history_.size() - 1 - i];
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
            sum = sum + val;
        }
        
        T mean = sum / static_cast<float>(count);
        
        // Calculate standard deviation
        T variance_sum = T{};
        for (size_t i = 0; i < count; ++i) {
            T diff = history_[history_.size() - 1 - i] - mean;
            variance_sum = variance_sum + (diff * diff);
        }
        
        float variance = /* magnitude of variance_sum */ / count;
        float stddev = std::sqrt(variance);
        
        return StatisticsData<T>{
            .min = min_val,
            .max = max_val,
            .mean = mean,
            .stddev = stddev
        };
    }
};
```

## Provided Algorithms

**Separate Repository**: `commrat-algorithms`

### Basic Filters
- `MovingAverage<T>` - Simple moving average
- `ExponentialMovingAverage<T>` - EMA with alpha parameter
- `LowPassFilter<T>` - First-order low-pass filter
- `HighPassFilter<T>` - First-order high-pass filter
- `MedianFilter<T>` - Median filter (requires Comparable)

### Statistical
- `WindowedStats<T>` - Min, max, mean, stddev over window
- `OutlierRejection<T>` - Remove values outside N sigma
- `Histogram<T>` - Binned histogram over window

### Advanced
- `KalmanFilter<T>` - Linear Kalman filter
- `ParticleFilter<T>` - Non-linear particle filter
- `PIDController<T>` - Proportional-Integral-Derivative controller

### Utility
- `RateLimiter<T>` - Limit output update rate
- `Deadband<T>` - Only update if change exceeds threshold
- `Hysteresis<T>` - Schmitt trigger behavior
- `Saturator<T>` - Clamp values to range

## User-Defined Operators

Users can enable generic algorithms for custom types:

```cpp
struct Quaternion {
    float w, x, y, z;
    
    // Enable arithmetic operations
    Quaternion operator+(const Quaternion& other) const;
    Quaternion operator-(const Quaternion& other) const;
    Quaternion operator*(float scalar) const;
    Quaternion operator/(float scalar) const;
    
    // Enable comparisons (magnitude-based)
    bool operator<(const Quaternion& other) const {
        return norm() < other.norm();
    }
    
    float norm() const {
        return std::sqrt(w*w + x*x + y*y + z*z);
    }
};

// Now can use:
using QuatFilter = MovingAverage<MyApp, Quaternion>;
using QuatStats = WindowedStats<MyApp, Quaternion>;
```

## Benefits

1. **Code Reuse**: Write once, use with any compatible type
2. **Type Safety**: Concepts enforce required operations at compile time
3. **Zero Overhead**: Generic code generates specialized implementations
4. **Consistency**: All algorithms follow same patterns and conventions
5. **Testing**: Algorithms tested once, work for all types
6. **Documentation**: Clear requirements via concepts

## Implementation Plan

**Phase 1**: Define concepts and base patterns (1 week)
**Phase 2**: Implement basic filters (MovingAverage, LowPass, etc.) (2 weeks)
**Phase 3**: Statistical algorithms (Windowed stats, outlier rejection) (2 weeks)
**Phase 4**: Advanced algorithms (Kalman, PID) (3 weeks)
**Phase 5**: Documentation and examples (1 week)

**Total Estimated Effort**: 9 weeks (separate from CommRaT core)

## Related Work

- C++20 Concepts: https://en.cppreference.com/w/cpp/language/constraints
- Module base: `include/commrat/registry_module.hpp`
- RingBuffer: SeRTial library
- Parameter system: `docs/work/PARAMETER_SYSTEM.md`
