# SeRTial RingBuffer Implementation Request

**Date**: February 6, 2026  
**Requester**: CommRaT Project  
**Purpose**: Realtime-safe circular buffer for CommRaT's historical data mechanism

---

## Overview

CommRaT needs a **realtime-safe ring buffer** for storing recent messages in producer modules. This enables time-synchronized multi-input processing similar to RACK's getData mechanism.

**Key Requirements:**
- ✅ **Zero runtime allocation** - fully compile-time sized
- ✅ **Fixed capacity** - template parameter `MaxSize`
- ✅ **Circular buffer** - oldest data overwritten when full
- ✅ **Header-only** - template implementation in `sertial/ring_buffer.hpp`
- ✅ **Realtime-safe** - no malloc/free, no exceptions, deterministic O(1) operations
- ✅ **STL-compatible** - iterator support, standard container interface where possible

---

## Template Signature

```cpp
namespace sertial {

template<typename T, size_t MaxSize>
class RingBuffer {
    // Fixed-capacity circular buffer
    // MaxSize: Compile-time capacity (e.g., 100 messages)
    // T: Element type (must be copyable or movable)
};

} // namespace sertial
```

---

## Required Interface

### 1. Construction and Capacity

```cpp
// Default constructor - empty buffer
constexpr RingBuffer() noexcept;

// Get maximum capacity (compile-time constant)
static constexpr size_t capacity() noexcept { return MaxSize; }

// Get current number of elements
constexpr size_t size() const noexcept;

// Check if buffer is empty
constexpr bool empty() const noexcept { return size() == 0; }

// Check if buffer is full
constexpr bool full() const noexcept { return size() == MaxSize; }

// Clear all elements (does not deallocate)
constexpr void clear() noexcept;
```

### 2. Element Access

```cpp
// Access element by index (0 = oldest, size()-1 = newest)
// Undefined behavior if index >= size()
constexpr T& operator[](size_t index) noexcept;
constexpr const T& operator[](size_t index) const noexcept;

// Access element with bounds checking (throws std::out_of_range if invalid)
// Note: Throws in non-RT context only - RT code should use operator[]
constexpr T& at(size_t index);
constexpr const T& at(size_t index) const;

// Access oldest element (front)
constexpr T& front() noexcept;
constexpr const T& front() const noexcept;

// Access newest element (back)
constexpr T& back() noexcept;
constexpr const T& back() const noexcept;
```

### 3. Modifiers

```cpp
// Push element to back (newest position)
// If full, overwrites oldest element
constexpr void push_back(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>);
constexpr void push_back(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>);

// Construct element in-place at back
template<typename... Args>
constexpr void emplace_back(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>);

// Remove oldest element (front)
// Undefined behavior if empty
constexpr void pop_front() noexcept;

// Remove newest element (back)
// Undefined behavior if empty
constexpr void pop_back() noexcept;
```

### 4. Iterators (Optional but Recommended)

```cpp
// Forward iterator over elements (oldest to newest)
constexpr iterator begin() noexcept;
constexpr const_iterator begin() const noexcept;
constexpr const_iterator cbegin() const noexcept;

constexpr iterator end() noexcept;
constexpr const_iterator end() const noexcept;
constexpr const_iterator cend() const noexcept;

// Iterator type (forward iterator requirements)
class iterator {
    // Standard iterator interface
    // operator++, operator*, operator->, operator==, operator!=
};

class const_iterator {
    // Const version of iterator
};
```

---

## Implementation Strategy

### Option 1: std::array-based (Recommended)

**Advantages**: Simple, standard, constexpr-friendly, zero overhead

```cpp
template<typename T, size_t MaxSize>
class RingBuffer {
private:
    std::array<T, MaxSize> data_{};  // Fixed storage
    size_t head_{0};                 // Write position (next insert)
    size_t tail_{0};                 // Read position (oldest element)
    size_t size_{0};                 // Current element count
    
public:
    constexpr void push_back(const T& value) noexcept(...) {
        data_[head_] = value;
        head_ = (head_ + 1) % MaxSize;
        
        if (size_ < MaxSize) {
            ++size_;
        } else {
            // Buffer full - advance tail (overwrite oldest)
            tail_ = (tail_ + 1) % MaxSize;
        }
    }
    
    constexpr const T& operator[](size_t index) const noexcept {
        // Map logical index to physical index
        return data_[(tail_ + index) % MaxSize];
    }
    
    // ... rest of implementation
};
```

### Option 2: Raw array with std::aligned_storage (Advanced)

**Only if needed for non-default-constructible types**

```cpp
template<typename T, size_t MaxSize>
class RingBuffer {
private:
    alignas(T) std::byte storage_[MaxSize * sizeof(T)];
    size_t head_{0};
    size_t tail_{0};
    size_t size_{0};
    
    T* data() noexcept {
        return std::launder(reinterpret_cast<T*>(storage_));
    }
    
    // Manual lifetime management (placement new/explicit destructor)
    // More complex but allows non-default-constructible T
};
```

**Recommendation**: Use Option 1 (std::array) unless there's a strong reason for Option 2.

---

## Usage Example (CommRaT Context)

```cpp
#include <sertial/ring_buffer.hpp>

// Message type from CommRaT
struct TimsMessage {
    uint64_t timestamp;
    uint32_t seq_number;
    uint8_t payload[256];
};

// Create ring buffer with 100-message capacity
sertial::RingBuffer<TimsMessage, 100> buffer;

// Producer: Add messages (realtime loop)
void producer_loop() {
    while (running) {
        TimsMessage msg = create_message();
        buffer.push_back(msg);  // O(1), no allocation, overwrites oldest
    }
}

// Consumer: Find message by timestamp
std::optional<TimsMessage> get_at_timestamp(uint64_t target_ts) {
    if (buffer.empty()) {
        return std::nullopt;
    }
    
    // Linear search for closest timestamp (RACK pattern)
    size_t best_idx = buffer.size() - 1;
    uint64_t min_diff = UINT64_MAX;
    
    for (size_t i = 0; i < buffer.size(); ++i) {
        uint64_t diff = std::abs(static_cast<int64_t>(
            buffer[i].timestamp - target_ts));
        
        if (diff <= min_diff) {
            min_diff = diff;
            best_idx = i;
        } else {
            break;  // Timestamps increasing, found minimum
        }
    }
    
    return buffer[best_idx];
}

// Get latest message
TimsMessage latest = buffer.back();  // Newest

// Get oldest message
TimsMessage oldest = buffer.front();  // Oldest

// Iterate all messages (oldest to newest)
for (const auto& msg : buffer) {
    process(msg);
}
```

---

## Thread Safety

**NOT thread-safe by default** - callers must provide external synchronization.

Rationale:
- Different use cases need different locking strategies
- CommRaT uses reader-writer locks (SharedMutex) for efficient multi-reader access
- Single lock for all operations would be inefficient

CommRaT will wrap with:
```cpp
class ThreadSafeRingBuffer {
    sertial::RingBuffer<T, MaxSize> buffer_;
    mutable std::shared_mutex mutex_;
    
public:
    void push(const T& value) {
        std::unique_lock lock(mutex_);  // Exclusive write
        buffer_.push_back(value);
    }
    
    std::optional<T> get(size_t index) const {
        std::shared_lock lock(mutex_);  // Shared read
        if (index >= buffer_.size()) return std::nullopt;
        return buffer_[index];
    }
};
```

---

## Performance Requirements

### Time Complexity (All O(1)):
- `push_back()`: O(1)
- `pop_front()`: O(1)
- `operator[]`: O(1)
- `size()`, `empty()`, `full()`: O(1)
- `front()`, `back()`: O(1)

### Space Complexity:
- `sizeof(RingBuffer<T, N>) = sizeof(T) * N + O(1)` (3 size_t for head/tail/size)

### Memory Usage Example:
```cpp
struct Message { uint8_t data[256]; uint64_t timestamp; };
RingBuffer<Message, 100> buffer;
// Size = 264 bytes * 100 + 24 bytes (metadata) = ~26 KB
```

---

## Constraints and Requirements

### Type Requirements for T:
```cpp
static_assert(std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>,
              "T must be copy or move constructible");

// Recommended but not required:
static_assert(std::is_trivially_copyable_v<T>,
              "For best performance, T should be trivially copyable");
```

### Compile-Time Validation:
```cpp
static_assert(MaxSize > 0, "MaxSize must be greater than 0");
static_assert(MaxSize <= std::numeric_limits<size_t>::max() / sizeof(T),
              "MaxSize * sizeof(T) would overflow");
```

### Behavior When Full:
- **Overwrite oldest** (RACK behavior)
- Alternative: Block or return error (not needed for CommRaT)

---

## Testing Requirements

Please provide tests for:

### 1. Basic Operations
```cpp
RingBuffer<int, 5> buf;
assert(buf.empty());
assert(buf.capacity() == 5);

buf.push_back(1);
buf.push_back(2);
assert(buf.size() == 2);
assert(buf.front() == 1);
assert(buf.back() == 2);
```

### 2. Wrap-Around
```cpp
RingBuffer<int, 3> buf;
buf.push_back(1);
buf.push_back(2);
buf.push_back(3);
buf.push_back(4);  // Overwrites 1
assert(buf.size() == 3);
assert(buf.front() == 2);  // 1 was overwritten
assert(buf.back() == 4);
```

### 3. Indexing
```cpp
RingBuffer<int, 5> buf;
for (int i = 0; i < 5; ++i) buf.push_back(i);
for (int i = 0; i < 5; ++i) assert(buf[i] == i);
```

### 4. Iterators
```cpp
RingBuffer<int, 5> buf;
for (int i = 0; i < 5; ++i) buf.push_back(i);

std::vector<int> vec(buf.begin(), buf.end());
assert(vec == std::vector<int>{0, 1, 2, 3, 4});
```

### 5. Non-Trivial Types
```cpp
struct Message {
    std::string data;
    int id;
};

RingBuffer<Message, 10> buf;
buf.push_back({"hello", 1});
assert(buf.front().data == "hello");
```

### 6. Constexpr Operations (C++20)
```cpp
constexpr bool test() {
    RingBuffer<int, 5> buf;
    buf.push_back(42);
    return buf.front() == 42;
}
static_assert(test());
```

---

## Header File Location

```
SeRTial/include/sertial/ring_buffer.hpp
```

**Include in CommRaT:**
```cpp
#include <sertial/ring_buffer.hpp>
```

---

## Additional Notes

### Why Not Use Boost.Circular_Buffer?
- Heavy dependency (requires Boost)
- May allocate dynamically
- Not necessarily realtime-safe
- Overkill for our needs

### Why Not Use std::deque?
- Dynamic allocation
- Not realtime-safe
- No fixed capacity guarantee

### Why Not std::vector with manual wrap-around?
- std::vector can reallocate
- Less ergonomic API
- RingBuffer makes intent clearer

---

## Priority

**High** - Needed for CommRaT Phase 6 (multi-input synchronization)

---

## Questions for SeRTial Team

1. Should we support custom allocators? (Probably not - defeats fixed-size purpose)
2. Should we provide `try_push_back()` that returns false when full? (Useful for non-overwriting use case)
3. Should iterator be bidirectional or just forward? (Forward is sufficient, simpler)
4. Should we support `reserve()` or `resize()`? (No - fixed capacity is the whole point)
5. Exception safety guarantees? (Basic guarantee for non-RT, nothrow for RT operations)

---

## Summary

**What We Need:**
- Header-only `RingBuffer<T, MaxSize>` template class
- Fixed capacity, zero allocation, circular buffer semantics
- Oldest-overwrite behavior when full
- Standard container-like interface (operator[], iterators, etc.)
- Realtime-safe (no malloc, no exceptions in hot path, deterministic timing)

**Target Use Case:**
- Store ~100 recent messages in producer module
- Allow consumers to fetch historical data by timestamp
- Support concurrent readers with external locking (SharedMutex)

**Timeline:**
- Needed before CommRaT Phase 6 (target: May 2026)
- Ideally available by Phase 5.5 (April 2026)

---

Thank you for your help with this foundational data structure! 🚀
