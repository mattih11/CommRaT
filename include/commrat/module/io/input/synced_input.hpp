#pragma once

#include "cmd_input.hpp"
#include "commrat/mailbox/mailbox.hpp"
#include "commrat/mailbox/timestamped_ring_buffer.hpp"
#include "commrat/platform/timestamp.hpp"
#include "commrat/module/io/synced.hpp"
#include "commrat/messaging/system/data_request_messages.hpp"

namespace commrat {

/**
 * @brief Synchronized input interface - pull data by timestamp (RACK getData)
 * 
 * Retrieves data via pull model using timestamp synchronization.
 * Does NOT receive continuous stream - instead queries producer's buffer
 * for data at specific timestamp (RACK-style get_data).
 * 
 * Flow (RACK getData pattern):
 * 1. Primary input drives execution with timestamp T
 * 2. get_data(T, tolerance) sends GetDataRequest to producer's CMD mailbox
 * 3. Producer's OutputBuffer searches for closest message
 * 4. Producer sends GetDataReply with message (or nullptr)
 * 5. Framework passes synchronized data to process()
 * 
 * Alternative (RACK getNextData):
 * - get_next_data() requests next available message
 * - One-shot subscription pattern
 * 
 * NO continuous stream - only RPC queries on demand
 * NO local buffer - producer's OutputBuffer is source of truth
 * 
 * @tparam Registry Message registry
 * @tparam OutputType Output message type (e.g., SensorData) - determines type_id
 * 
 * RACK Equivalent: RackDataProxy::getData() + getNextData()
 * Modern approach: Type-safe RPC, compile-time type_id
 */
template<typename Registry, typename OutputType>
class SyncedInputImpl : public CmdInput<Registry, OutputType> {
public:
    // Type exposure for infrastructure
    using DataMessage = Message::Data<OutputType>;
    
    /**
     * @brief Construct synchronized input
     * @param work_mbx Shared mailbox for get_data replies
     * @param producer_system_id Producer's system ID
     * @param producer_instance_id Producer's instance ID
     * @param tolerance Maximum time difference for get_data matching
     * @param interpolation How to handle timestamp mismatches
     * @param cmd_timeout Timeout for get_data RPC
     */
    SyncedInputImpl(MailboxFor<Registry>& work_mbx,
                    uint8_t producer_system_id,
                    uint8_t producer_instance_id,
                    Milliseconds tolerance = Milliseconds(50),
                    InterpolationMode interpolation = InterpolationMode::NEAREST,
                    Milliseconds cmd_timeout = Milliseconds(100))
        : CmdInput<Registry, OutputType>(work_mbx, producer_system_id, producer_instance_id, cmd_timeout)
        , tolerance_(tolerance)
        , interpolation_(interpolation)
        , last_get_succeeded_(false)
        , last_get_was_fresh_(false)
    {}
    
    /**
     * @brief Get data synchronized to timestamp (RACK getData)
     * 
     * Sends GetDataRequest RPC to producer's CMD mailbox.
     * Producer searches its OutputBuffer and responds with GetDataReply.
     * 
     * RACK getData pattern:
     * 1. Build GetDataRequest with target timestamp
     * 2. Send to producer's CMD mailbox
     * 3. Producer's OutputBuffer.getData() searches buffer
     * 4. Producer sends GetDataReply to work_mbx
     * 5. Receive reply and reconstruct TimsMessage<T>
     * 
     * @param timestamp Target timestamp to synchronize to
     * @param msg Output parameter - pointer set to received message if found
     * @param timeout RPC timeout (default: constructor value)
     * @return True if data found within tolerance, false otherwise
     */
    bool get_data(const Timestamp& timestamp, const TimsMessage<OutputType>*& msg, 
                 Milliseconds timeout = Milliseconds(0)) {
        // Type aliases for GetData messages (type-specific)
        using GetDataReqPayload = GetDataRequestPayload<OutputType>;
        using GetDataRepPayload = GetDataReplyPayload<OutputType>;
        
        // Build request
        TimsMessage<GetDataReqPayload> request{
            .header = {
                .timestamp = Time::now(),
                .seq_number = 0,
                .msg_type = Registry::template get_message_id<GetDataReqPayload>(),
                .src_address = 0,  // Will be filled by mailbox
                .dest_address = 0  // Will be filled by mailbox
            },
            .payload = {
                .target_timestamp = timestamp,
                .tolerance_ns = std::chrono::duration_cast<Nanoseconds>(tolerance_).count(),
                .interpolation_mode = static_cast<uint8_t>(interpolation_)
            }
        };
        
        // Send and receive reply
        TimsMessage<GetDataRepPayload> reply;
        if (!this->send_command(request, reply, timeout)) {
            msg = nullptr;
            last_get_succeeded_ = false;
            last_get_was_fresh_ = false;
            return false;
        }
        
        // Check if data found
        if (!reply.payload.found) {
            msg = nullptr;
            last_get_succeeded_ = false;
            last_get_was_fresh_ = false;
            return false;
        }
        
        // Reconstruct TimsMessage<T> from reply (data + metadata)
        last_message_.header.timestamp = reply.payload.data_timestamp;
        last_message_.header.seq_number = 0;  // Producer's seq number (could pass through)
        last_message_.header.msg_type = Registry::template get_message_id<OutputType>();
        last_message_.header.src_address = reply.header.src_address;
        last_message_.header.dest_address = reply.header.dest_address;
        last_message_.payload = reply.payload.data;
        
        msg = &last_message_;
        last_get_succeeded_ = true;
        
        // Determine freshness from timestamp delta (SyncedInput's responsibility)
        last_get_was_fresh_ = (reply.payload.timestamp_delta_ns == 0);
        
        return true;
    }
    
    /**
     * @brief Get next available data (RACK getNextData)
     * 
     * One-shot subscription: request next message without timestamp matching.
     * 
     * @param msg Output parameter - pointer set to received message
     * @param timeout RPC timeout
     * @return True if data received, false on timeout
     */
    bool get_next_data(const TimsMessage<OutputType>*& msg, Milliseconds timeout = Milliseconds(0)) {
        // Type aliases for GetNextData messages (type-specific)
        using GetNextDataReqPayload = GetNextDataRequestPayload<OutputType>;
        using GetNextDataRepPayload = GetNextDataReplyPayload<OutputType>;
        
        // Build request
        TimsMessage<GetNextDataReqPayload> request{
            .header = {
                .timestamp = Time::now(),
                .seq_number = 0,
                .msg_type = Registry::template get_message_id<GetNextDataReqPayload>(),
                .src_address = 0,  // Will be filled by mailbox
                .dest_address = 0  // Will be filled by mailbox
            },
            .payload = {}  // Empty
        };
        
        // Send and receive reply
        TimsMessage<GetNextDataRepPayload> reply;
        if (!this->send_command(request, reply, timeout)) {
            msg = nullptr;
            last_get_succeeded_ = false;
            last_get_was_fresh_ = false;
            return false;
        }
        
        // Check if data found
        if (!reply.payload.found) {
            msg = nullptr;
            last_get_succeeded_ = false;
            last_get_was_fresh_ = false;
            return false;
        }
        
        // Reconstruct TimsMessage<T> from reply (data + metadata)
        last_message_.header.timestamp = reply.payload.data_timestamp;
        last_message_.header.seq_number = 0;
        last_message_.header.msg_type = Registry::template get_message_id<OutputType>();
        last_message_.header.src_address = reply.header.src_address;
        last_message_.header.dest_address = reply.header.dest_address;
        last_message_.payload = reply.payload.data;
        
        msg = &last_message_;
        last_get_succeeded_ = true;
        last_get_was_fresh_ = true;  // Next data is always "fresh"
        return true;
    }
    
    /**
     * @brief Check if last get_data succeeded
     * Used by framework to track data validity for process()
     */
    bool is_valid() const { return last_get_succeeded_; }
    
    /**
     * @brief Check if get_data returned fresh data (not cached)
     */
    bool is_fresh() const { return last_get_was_fresh_; }
    
    /**
     * @brief Get payload as Synced<T> wrapper (zero-copy)
     * 
     * Returns Synced wrapper containing reference to payload if get_data succeeded.
     * Used by Module2 to build process() parameters.
     * 
     * @return Synced<OutputType> with validity and freshness metadata
     */
    Synced<OutputType> get_payload() const {
        if (last_get_succeeded_) {
            return Synced<OutputType>(last_message_.payload, last_get_was_fresh_);
        }
        return Synced<OutputType>();  // Invalid
    }
    
    /**
     * @brief Get header (zero-copy)
     * 
     * @return const TimsHeader& reference to last message header
     * @note Returns default header if no data retrieved yet
     */
    const TimsHeader& get_header() const {
        return last_message_.header;
    }
    
    /**
     * @brief Get timestamp from last retrieved message
     * @return Timestamp (nanoseconds since epoch)
     */
    uint64_t get_timestamp() const {
        return last_message_.header.timestamp;
    }
    
    /**
     * @brief Get sequence number from last message
     */
    uint32_t get_sequence_number() const {
        return last_message_.header.seq_number;
    }
    
    /**
     * @brief Get message type ID
     */
    uint32_t get_message_id() const {
        return last_message_.header.msg_type;
    }
    
private:
    Milliseconds tolerance_;                ///< Maximum time difference for matching
    InterpolationMode interpolation_;       ///< How to handle mismatches
    bool last_get_succeeded_;               ///< Did last get_data find data?
    bool last_get_was_fresh_;               ///< Was last get_data fresh or cached?
    TimsMessage<OutputType> last_message_;  ///< Storage for last retrieved message
};

} // namespace commrat
