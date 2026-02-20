#pragma once

#include "cmd_input.hpp"
#include "commrat/timestamp.hpp"
#include <optional>

/**
 * @brief Synchronized input interface - getData by timestamp
 * 
 * Retrieves data via pull model using timestamp synchronization.
 * Does NOT receive continuous stream - instead queries producer's buffer
 * for data at specific timestamp (RACK-style getData).
 * 
 * Flow:
 * 1. Subscribe to producer via work_mbx (optional - may just use getData)
 * 2. Primary input drives execution with timestamp T
 * 3. get_data(T, tolerance) sends RPC to producer's buffer
 * 4. Producer returns closest data within tolerance window
 * 5. Framework passes synchronized data to process()
 * 
 * NO data_mbx - no continuous stream, only RPC queries
 * NO local buffer - producer's buffer is the source of truth
 * 
 * Architecture inspired by RACK framework (github.com/smolorz/RACK)
 * This abstracts RACK's getData mechanism for multi-rate sensor fusion
 */
template<typename CommratApp, typename T>
class SyncedInput : public CmdInput<CommratApp, T>
{
public:
    using Type = T;
    using WorkMailbox = TypedMailbox<typename CommratApp::SystemRegistry::SubscriptionAck>;
    
    /**
     * @brief Construct synchronized input
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     * @param work_mbx Reference to module's shared work mailbox (optional for getData-only)
     * @param tolerance Maximum time difference for getData matching
     * @param interpolation How to handle timestamp mismatches
     */
    SyncedInput(SystemId producer_system_id, 
                InstanceId producer_instance_id,
                std::optional<std::reference_wrapper<WorkMailbox>> work_mbx = std::nullopt,
                Duration tolerance = Milliseconds(50),
                InterpolationMode interpolation = InterpolationMode::Nearest)
        : CmdInput<CommratApp, T>(producer_system_id, producer_instance_id)
        , work_mbx_(work_mbx)
        , tolerance_(tolerance)
        , interpolation_(interpolation)
    {}
    
    /**
     * @brief Subscribe to producer (optional for getData)
     * May be needed for producer to start buffering data
     * @return Success or error
     */
    auto subscribe() -> MailboxResult<void> {
        if (!work_mbx_) {
            return MailboxResult<void>::error("No work mailbox configured");
        }
        // TODO: Send SubscribeRequest via work_mbx_ to this->producer_cmd_address_
        return MailboxResult<void>::success();
    }
    
    /**
     * @brief Get data synchronized to timestamp
     * 
     * Sends RPC to producer's buffer requesting data at specified timestamp.
     * Producer searches its RingBuffer for closest match within tolerance.
     * 
     * @param timestamp Target timestamp to synchronize to
     * @return Data at timestamp or nullopt if no match within tolerance
     */
    auto get_data(const Timestamp& timestamp) -> std::optional<T> {
        // TODO: Implement RPC to producer's buffer
        // 1. Send GetDataRequest(timestamp, tolerance_) to producer
        // 2. Producer searches RingBuffer for match
        // 3. Producer returns GetDataResponse(data) or error
        // 4. Return data or nullopt
        return std::nullopt;
    }
    
    /**
     * @brief Check if last getData succeeded
     * Used by framework to track data validity for process()
     */
    bool is_valid() const { return last_get_succeeded_; }
    
    /**
     * @brief Check if getData returned fresh data (not cached)
     */
    bool is_fresh() const { return last_get_was_fresh_; }
    
private:
    std::optional<std::reference_wrapper<WorkMailbox>> work_mbx_;  // Optional work mailbox
    Duration tolerance_;                // Maximum time difference for matching
    InterpolationMode interpolation_;   // How to handle mismatches
    bool last_get_succeeded_ = false;   // Did last getData find data?
    bool last_get_was_fresh_ = false;   // Was last getData fresh or cached?
};
