/**
 * @file output_infrastructure.hpp
 * @brief Output infrastructure for structured output access
 * 
 * Provides:
 * - Named output struct builder (reflect-cpp integration)
 * - Type-based field naming with duplicate handling
 * - Index-based and name-based output access
 */

#pragma once

#include "commrat/module/io/output/module_output.hpp"
#include "commrat/module/io/io_spec.hpp"
#include "commrat/module/helpers/type_name.hpp"
#include <rfl.hpp>
#include <tuple>

namespace commrat {

/**
 * @brief Output infrastructure helpers
 * 
 * Thin wrapper providing ONLY structured named access to outputs.
 * All indexing and metadata delegated to IOBuilder.
 * 
 * @tparam IOBuilder The BuildIOTuple type (provides all indexing/metadata)
 * @tparam IOTuple The actual I/O tuple type
 */
template<typename IOBuilder, typename IOTuple>
class OutputInfrastructure {
protected:
    /**
     * @brief Build rfl::NamedTuple with fields for each output
     * 
     * Creates struct with named fields based on output payload types.
     * Returns references to actual io_tuple elements (zero-copy).
     */
    template<size_t... Indices>
    static auto build_named_output_struct(IOTuple& io_tuple, std::index_sequence<Indices...>) {
        return rfl::make_named_tuple(
            make_output_field<Indices>(io_tuple)...
        );
    }
    
    template<size_t... Indices>
    static auto build_named_output_struct(const IOTuple& io_tuple, std::index_sequence<Indices...>) {
        return rfl::make_named_tuple(
            make_output_field<Indices>(io_tuple)...
        );
    }

private:
    /**
     * @brief Create named field for output at logical Index
     */
    template<size_t Index>
    static auto make_output_field(IOTuple& io_tuple) {
        // Delegate to IOBuilder for all indexing
        auto& output = IOBuilder::template get_output<Index>(io_tuple);
        using OutputType = std::decay_t<decltype(output)>;
        
        // Generate field name with suffix for duplicates
        constexpr auto field_name = generate_output_field_name<Index>();
        
        // Return rfl::Field with reference to actual tuple element
        return rfl::Field<field_name, OutputType&>(output);
    }
    
    template<size_t Index>
    static auto make_output_field(const IOTuple& io_tuple) {
        auto& output = IOBuilder::template get_output<Index>(io_tuple);
        using OutputType = std::decay_t<decltype(output)>;
        constexpr auto field_name = generate_output_field_name<Index>();
        return rfl::Field<field_name, const OutputType&>(output);
    }
    
    /**
     * @brief Generate field name: "type_name" or "type_name_N" for duplicates
     * 
     * Uses payload type name from ModuleOutput<Registry, DataType>.
     * Adds _N suffix if same type appears multiple times.
     */
    template<size_t Index>
    static constexpr auto generate_output_field_name() {
        // Delegate to IOBuilder for index mapping
        constexpr auto indices = IOBuilder::output_indices();
        constexpr size_t tuple_index = indices[Index];
        
        using OutputType = std::tuple_element_t<tuple_index, IOTuple>;
        
        // Extract payload type from ModuleOutput<Registry, DataType>
        using PayloadType = typename OutputType::DataMessage::Payload;
        constexpr auto base_name = TypeName<PayloadType>::value;
        
        // Count how many times this payload type appears before Index
        constexpr size_t occurrence_index = count_output_type_occurrences_before<PayloadType, Index>();
        
        // If this is first occurrence, use base name; otherwise append _N
        if constexpr (occurrence_index == 0) {
            return base_name;
        } else {
            // TODO: Implement to_fixed_string helper for duplicate output naming
            // For now, just use base name (duplicates will have same name)
            return base_name;
            // constexpr auto suffix = sertial::fixed_string("_") + sertial::to_fixed_string(occurrence_index);
            // return base_name + suffix;
        }
    }
    
    /**
     * @brief Count how many times PayloadType appears in outputs before Index
     */
    template<typename PayloadType, size_t BeforeIndex>
    static constexpr size_t count_output_type_occurrences_before() {
        constexpr auto indices = IOBuilder::output_indices();
        size_t count = 0;
        
        for (size_t i = 0; i < BeforeIndex; ++i) {
            constexpr size_t tuple_index = indices[i];
            using OutputType = std::tuple_element_t<tuple_index, IOTuple>;
            using CurrentPayloadType = typename OutputType::DataMessage::Payload;
            
            if constexpr (std::is_same_v<PayloadType, CurrentPayloadType>) {
                count++;
            }
        }
        
        return count;
    }
};

/**
 * @brief Input infrastructure helpers
 * 
 * Thin wrapper providing ONLY structured named access to inputs.
 * All indexing and metadata delegated to IOBuilder.
 * 
 * @tparam IOBuilder The BuildIOTuple type (provides all indexing/metadata)
 * @tparam IOTuple The actual I/O tuple type
 */
template<typename IOBuilder, typename IOTuple>
class InputInfrastructure {
protected:
    /**
     * @brief Build rfl::NamedTuple with fields for each input
     * 
     * Creates struct with named fields based on input payload types.
     * Returns references to actual io_tuple elements (zero-copy).
     */
    template<size_t... Indices>
    static auto build_named_input_struct(IOTuple& io_tuple, std::index_sequence<Indices...>) {
        return rfl::make_named_tuple(
            make_input_field<Indices>(io_tuple)...
        );
    }
    
    template<size_t... Indices>
    static auto build_named_input_struct(const IOTuple& io_tuple, std::index_sequence<Indices...>) {
        return rfl::make_named_tuple(
            make_input_field<Indices>(io_tuple)...
        );
    }

private:
    /**
     * @brief Create named field for input at logical Index
     */
    template<size_t Index>
    static auto make_input_field(IOTuple& io_tuple) {
        // Delegate to IOBuilder for all indexing
        auto& input = IOBuilder::template get_input<Index>(io_tuple);
        using InputType = std::decay_t<decltype(input)>;
        
        // Generate field name with suffix for duplicates
        constexpr auto field_name = generate_input_field_name<Index>();
        
        // Return rfl::Field with reference to actual tuple element
        return rfl::Field<field_name, InputType&>(input);
    }
    
    template<size_t Index>
    static auto make_input_field(const IOTuple& io_tuple) {
        auto& input = IOBuilder::template get_input<Index>(io_tuple);
        using InputType = std::decay_t<decltype(input)>;
        constexpr auto field_name = generate_input_field_name<Index>();
        return rfl::Field<field_name, const InputType&>(input);
    }
    
    /**
     * @brief Generate field name: "type_name" or "type_name_N" for duplicates
     * 
     * Uses payload type name from ContinuousInput/SyncedInput<Registry, DataType>.
     * Adds _N suffix if same type appears multiple times.
     */
    template<size_t Index>
    static constexpr auto generate_input_field_name() {
        // Delegate to IOBuilder for index mapping
        constexpr auto indices = IOBuilder::input_indices();
        constexpr size_t tuple_index = indices[Index];
        
        using InputType = std::tuple_element_t<tuple_index, IOTuple>;
        
        // Extract payload type from ContinuousInput/SyncedInput
        using PayloadType = typename InputType::DataMessage::Payload;
        constexpr auto base_name = TypeName<PayloadType>::value;
        
        // Count occurrences of this type before current index
        constexpr size_t occurrence_index = count_input_type_occurrences_before<PayloadType, Index>();
        
        if constexpr (occurrence_index == 0) {
            return base_name;
        } else {
            constexpr auto suffix = sertial::make_fixed<"_">() + 
                                   sertial::make_fixed_string<1>(static_cast<char[1]>('0' + occurrence_index));
            return base_name + suffix;
        }
    }
    
    /**
     * @brief Count how many times PayloadType appears in inputs before Index
     */
    template<typename PayloadType, size_t BeforeIndex>
    static constexpr size_t count_input_type_occurrences_before() {
        constexpr auto indices = IOBuilder::input_indices();
        size_t count = 0;
        
        for (size_t i = 0; i < BeforeIndex; ++i) {
            constexpr size_t tuple_index = indices[i];
            using InputType = std::tuple_element_t<tuple_index, IOTuple>;
            using CurrentPayloadType = typename InputType::DataMessage::Payload;
            
            if constexpr (std::is_same_v<PayloadType, CurrentPayloadType>) {
                count++;
            }
        }
        
        return count;
    }
};

} // namespace commrat
