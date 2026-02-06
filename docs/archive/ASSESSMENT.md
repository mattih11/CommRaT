# ASSESSMENT: What Needs to be Fixed

## Current Situation

After checking the symlinked SeRTial and TIMS directories, I accidentally **rewrote functionality that already exists**. Here's what we need to fix:

---

## ❌ PROBLEMS IN CURRENT CODE

### 1. **FAKE SeRTial Implementation** (messages.hpp lines ~14-75)
I created placeholder implementations of:
- `sertial::fixed_vector<T, N>` 
- `sertial::fixed_string<N>`
- `sertial::Message<T>`

**Reality**: These already exist in:
- `/home/muddy/src/CommRaT/SeRTial/include/sertial/containers/fixed_vector.hpp`
- `/home/muddy/src/CommRaT/SeRTial/include/sertial/containers/fixed_string.hpp`
- `/home/muddy/src/CommRaT/SeRTial/include/sertial/message.hpp`

### 2. **Wrong SeRTial API Usage**
My code assumes:
```cpp
auto result = serialize_message(msg);
result.data()  // Returns uint8_t*
result.size    // Direct member
```

**Reality - Actual SeRTial API**:
```cpp
auto result = sertial::Message<T>::serialize(value);
result.view()          // Returns std::span<const std::byte>
result.buffer          // std::array<std::byte, N>
result.size            // Actual size written
```

### 3. **Return Types Are Wrong**
My wrapper returns custom `SerializeResult<T>` wrapper.

**Reality**: SeRTial returns `Message<T>::Result` which has:
- `buffer_type buffer` (std::array<std::byte, max_buffer_size>)
- `std::size_t size`
- `std::span<const std::byte> view()` method
- `std::span<std::byte> data()` method

### 4. **Deserialization API Wrong**
My code:
```cpp
std::optional<T> deserialize_message(...)
```

**Reality**: SeRTial returns:
```cpp
DeserializeResult<T> Message<T>::deserialize(std::span<const std::byte> data)
// Where DeserializeResult has operator bool() and operator* like std::expected
```

### 5. **TIMS Integration Issues**
The TIMS wrapper is mostly correct BUT needs updates for:
- Byte type: Using `uint8_t*` but SeRTial uses `std::byte`
- Size calculations need to use SeRTial's compile-time info
- Need proper conversion between `std::byte` and `uint8_t` for TIMS

---

## ✅ WHAT TO KEEP

### The TIMS API integration is CORRECT:
- `tims_socket()`, `tims_bind()`, `tims_mbx_create()`
- `tims_sendmsg()` with iovec
- `tims_recvmsg_timed()`
- `tims_fill_head()`, `tims_parse_head_byteorder()`

### The Message Structure Design is GOOD:
```cpp
template<typename PayloadT, MessageType MsgType>
struct Message {
    TimsHeader header;
    PayloadT payload;
};
```
This is the right approach!

---

## 🔧 REQUIRED FIXES

### 1. **Remove Fake SeRTial Implementations**
Delete lines ~14-75 in messages.hpp (the placeholder namespace sertial)

### 2. **Include Real SeRTial Headers**
```cpp
#include <sertial/sertial.hpp>
#include <sertial/containers/fixed_vector.hpp>
#include <sertial/containers/fixed_string.hpp>
```

### 3. **Fix Serialization Function**
```cpp
// Instead of custom wrapper, return SeRTial's Result directly
template<typename T>
auto serialize_message(T& message) -> typename sertial::Message<T>::Result {
    // Use SeRTial directly
    return sertial::Message<T>::serialize(message);
}
```

### 4. **Fix Deserialization Function**
```cpp
template<typename T>
auto deserialize_message(std::span<const std::byte> data) {
    return sertial::Message<T>::deserialize(data);
}
```

### 5. **Update TIMS Wrapper for std::byte**
Need converters between `std::byte*` and `uint8_t*` since:
- SeRTial uses `std::byte`
- TIMS expects `void*`

### 6. **Update CMakeLists.txt Paths**
```cmake
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/SeRTial/include  # Fixed path
    ${CMAKE_CURRENT_SOURCE_DIR}/tims              # Fixed path
)
```

### 7. **Use SeRTial's Compile-Time Info**
Replace my fake constexpr variables with real ones:
```cpp
sertial::Message<T>::max_buffer_size   // Not our custom one
sertial::Message<T>::packed_size
sertial::Message<T>::has_padding
```

---

## 📊 ACTUAL SeRTial API (from real code)

```cpp
// Message wrapper
template<typename T> requires BoundedSerializable<T>
struct Message {
    // Compile-time info
    static constexpr size_t max_buffer_size = ...;
    static constexpr size_t packed_size = ...;
    static constexpr bool has_padding = ...;
    
    // Buffer type
    using buffer_type = std::array<std::byte, max_buffer_size>;
    
    // Serialize result
    struct Result {
        buffer_type buffer{};
        std::size_t size = 0;
        
        std::span<const std::byte> view() const noexcept;
        std::span<std::byte> data() noexcept;
        explicit operator bool() const noexcept;
    };
    
    // API
    static Result serialize(const T& value);
    static DeserializeResult<T> deserialize(std::span<const std::byte> data);
};

// Containers
template<typename T, size_t N> class fixed_vector;
template<size_t N> class fixed_string;
```

---

## PRIORITY ORDER

1. **Remove fake SeRTial namespace** (highest priority)
2. **Include real SeRTial headers**
3. **Fix serialize_message() return type**
4. **Fix deserialize_message() return type**
5. **Update TIMS wrapper byte conversions**
6. **Update CMakeLists.txt include paths**
7. **Test compilation**

---

## BOTTOM LINE

I need to:
- **DELETE** ~60 lines of fake SeRTial implementation
- **INCLUDE** the real headers
- **FIX** API calls to match actual SeRTial
- **CONVERT** between `std::byte` and `uint8_t` for TIMS
- **UPDATE** CMake paths

The message structure design is good, TIMS integration is good, but I need to use the REAL SeRTial library instead of reinventing it!
