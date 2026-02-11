#pragma once

#include "commrat/module/metadata/input_metadata.hpp"
#include "commrat/messages.hpp"
#include <iostream>

namespace commrat {

/**
 * @brief Phase 9: Input Metadata Manager CRTP Mixin
 * 
 * Manages input metadata storage and updates:
 * - update_input_metadata(): Populate from received TimsMessage
 * - mark_input_invalid(): Mark input as stale/invalid (getData failed)
 * 
 * Used by loop executors and multi-input processors to maintain
 * accurate timestamp/freshness tracking for all inputs.
 * 
 * @tparam ModuleType The derived Module class (CRTP)
 */
template<typename ModuleType>
class InputMetadataManager {
protected:
    /**
     * @brief Update metadata for a specific input index
     * 
     * Helper method to populate metadata from received TimsMessage.
     * Called by loop functions before invoking process() methods.
     * 
     * @tparam T Payload type
     * @param index Input index (0-based)
     * @param received Received TimsMessage with header and payload
     * @param is_new True if freshly received, false if reused/stale
     */
    template<typename T>
    void update_input_metadata(std::size_t index, const TimsMessage<T>& received, bool is_new) {
        auto& module = static_cast<ModuleType&>(*this);
        
        if (index >= module.num_inputs) {
            std::cerr << "[Module] ERROR: Invalid metadata index " << index << "\n";
            return;
        }
        
        module.input_metadata_[index].timestamp = received.header.timestamp;
        module.input_metadata_[index].sequence_number = received.header.seq_number;
        module.input_metadata_[index].message_id = received.header.msg_type;
        module.input_metadata_[index].is_new_data = is_new;
        module.input_metadata_[index].is_valid = true;
    }
    
    /**
     * @brief Mark input metadata as invalid (getData failed)
     * 
     * Called when a secondary input's getData() fails in multi-input
     * synchronization. Marks the input as invalid and not fresh.
     * 
     * @param index Input index (0-based)
     */
    void mark_input_invalid(std::size_t index) {
        auto& module = static_cast<ModuleType&>(*this);
        
        if (index >= module.num_inputs) {
            return;
        }
        
        module.input_metadata_[index].is_valid = false;
        module.input_metadata_[index].is_new_data = false;
    }
};

}  // namespace commrat
