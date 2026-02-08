/**
 * @file input_metadata_accessors.hpp
 * @brief Input metadata accessor methods for Module classes
 * 
 * Phase 6.10: Provides get_input_metadata, get_input_timestamp, has_new_data,
 * and is_input_valid accessors for both index-based and type-based access.
 */

#pragma once

#include "commrat/module/metadata/input_metadata.hpp"
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace commrat {

/**
 * @brief Mixin providing input metadata accessor methods
 * 
 * CRTP base class that provides metadata accessors for modules with inputs.
 * Access patterns:
 * - Index-based: get_input_metadata<0>(), get_input_timestamp<1>()
 * - Type-based: get_input_metadata<IMUData>() (when types unique)
 * 
 * @tparam ModuleType The derived Module class (CRTP pattern)
 */
template<typename ModuleType>
class InputMetadataAccessors {
protected:
    // CRTP accessor to get derived module instance
    ModuleType& module() { return static_cast<ModuleType&>(*this); }
    const ModuleType& module() const { return static_cast<const ModuleType&>(*this); }
    
public:
    /**
     * @brief Input metadata structure returned by accessor methods
     * 
     * Contains timestamp, sequence number, and freshness information about
     * a received input message. Available for modules with continuous inputs.
     * 
     * @tparam T The payload type of the input
     */
    template<typename T>
    struct InputMetadata {
        uint64_t timestamp;          ///< Message timestamp (from TimsHeader)
        uint32_t sequence_number;    ///< Message sequence number
        uint32_t message_id;         ///< Message type ID
        bool is_new_data;            ///< True if freshly received, false if stale/reused
        bool is_valid;               ///< True if getData succeeded, false if failed
        
        // Helper to get input type (for debugging/logging)
        static constexpr const char* type_name() { return typeid(T).name(); }
    };
    
    /**
     * @brief Get metadata for input by index (always available)
     * 
     * Returns metadata about the input at the specified index.
     * Index 0 is always the primary input (or only input for single-input modules).
     * 
     * @tparam Index The input index (0 for primary/single, 1+ for multi-input secondaries)
     * @return InputMetadata containing timestamp, sequence, freshness, validity
     * 
     * @example
     * // Single input module
     * auto meta = get_input_metadata<0>();
     * assert(meta.timestamp > 0);
     * 
     * // Multi-input module
     * auto imu_meta = get_input_metadata<0>();  // Primary
     * auto gps_meta = get_input_metadata<1>();  // Secondary
     * if (!gps_meta.is_new_data) {
     *     std::cout << "GPS data is stale\\n";
     * }
     */
    template<std::size_t Index>
    auto get_input_metadata() const {
        constexpr std::size_t num_inputs = ModuleType::num_inputs;
        static_assert(Index < num_inputs, "Input index out of bounds");
        
        using InputType = std::conditional_t<
            (num_inputs == 1),
            typename ModuleType::InputData,  // Single input: use InputData directly
            std::tuple_element_t<Index, typename ModuleType::InputTypesTuple>  // Multi-input: extract from tuple
        >;
        
        const auto& storage = module().input_metadata_[Index];
        return InputMetadata<InputType>{
            .timestamp = storage.timestamp,
            .sequence_number = storage.sequence_number,
            .message_id = storage.message_id,
            .is_new_data = storage.is_new_data,
            .is_valid = storage.is_valid
        };
    }
    
    /**
     * @brief Get input timestamp by index (convenience method)
     * 
     * @tparam Index The input index
     * @return Timestamp from TimsHeader (nanoseconds since epoch)
     */
    template<std::size_t Index>
    uint64_t get_input_timestamp() const {
        constexpr std::size_t num_inputs = ModuleType::num_inputs;
        static_assert(Index < num_inputs, "Input index out of bounds");
        return module().input_metadata_[Index].timestamp;
    }
    
    /**
     * @brief Check if input has new data by index
     * 
     * For multi-input modules, returns true if the input was freshly received
     * and not reused from history (timestamp matches primary).
     * For single-input modules, always returns true.
     * 
     * @tparam Index The input index
     * @return True if fresh data, false if stale
     */
    template<std::size_t Index>
    bool has_new_data() const {
        constexpr std::size_t num_inputs = ModuleType::num_inputs;
        static_assert(Index < num_inputs, "Input index out of bounds");
        return module().input_metadata_[Index].is_new_data;
    }
    
    /**
     * @brief Check if input is valid by index
     * 
     * For multi-input modules, returns true if getData succeeded.
     * For single-input modules, always returns true (receive succeeded).
     * 
     * @tparam Index The input index
     * @return True if valid, false if getData failed
     */
    template<std::size_t Index>
    bool is_input_valid() const {
        constexpr std::size_t num_inputs = ModuleType::num_inputs;
        static_assert(Index < num_inputs, "Input index out of bounds");
        return module().input_metadata_[Index].is_valid;
    }
    
    // ========================================================================
    // Phase 6.10: Type-Based Metadata Accessors
    // ========================================================================
    
    /**
     * @brief Get metadata for input by type (only when types unique)
     * 
     * Returns metadata about the input with the specified payload type.
     * Only works when all input types are unique (compile error otherwise).
     * 
     * @tparam T The payload type of the input
     * @return InputMetadata for the input of type T
     * 
     * @example
     * // Multi-input module with unique types
     * auto imu_meta = get_input_metadata<IMUData>();
     * auto gps_meta = get_input_metadata<GPSData>();
     * 
     * // Compile error if types duplicate:
     * // Inputs<SensorData, SensorData> - can't use type-based access
     */
    template<typename T>
    auto get_input_metadata() const
        requires (ModuleType::num_inputs > 1)  // Only for multi-input
    {
        constexpr std::size_t index = find_type_index<T, 
            typename std::tuple_element<0, typename ModuleType::InputTypesTuple>::type,
            typename std::tuple_element<1, typename ModuleType::InputTypesTuple>::type
            // TODO: Need to unpack full tuple, not just first 2 elements
        >();
        return get_input_metadata<index>();
    }
    
    /**
     * @brief Get input timestamp by type (convenience method)
     */
    template<typename T>
    uint64_t get_input_timestamp() const
        requires (ModuleType::num_inputs > 1)
    {
        constexpr std::size_t index = find_type_index<T,
            typename std::tuple_element<0, typename ModuleType::InputTypesTuple>::type,
            typename std::tuple_element<1, typename ModuleType::InputTypesTuple>::type
            // TODO: Need to unpack full tuple
        >();
        return get_input_timestamp<index>();
    }
    
    /**
     * @brief Check if input has new data by type
     */
    template<typename T>
    bool has_new_data() const
        requires (ModuleType::num_inputs > 1)
    {
        constexpr std::size_t index = find_type_index<T,
            typename std::tuple_element<0, typename ModuleType::InputTypesTuple>::type,
            typename std::tuple_element<1, typename ModuleType::InputTypesTuple>::type
        >();
        return has_new_data<index>();
    }
    
    /**
     * @brief Check if input is valid by type
     */
    template<typename T>
    bool is_input_valid() const
        requires (ModuleType::num_inputs > 1)
    {
        constexpr std::size_t index = find_type_index<T,
            typename std::tuple_element<0, typename ModuleType::InputTypesTuple>::type,
            typename std::tuple_element<1, typename ModuleType::InputTypesTuple>::type
        >();
        return is_input_valid<index>();
    }
};

} // namespace commrat
