#pragma once

#include "../message_id.hpp"
#include "commrat/messages.hpp"
#include <cstdint>

namespace commrat {

// Forward declarations for ReplyType aliases
template<typename T>
struct GetDataReplyPayload;

template<typename T>
struct GetNextDataReplyPayload;

/**
 * @brief Request data synchronized to a specific timestamp (generic over data type)
 * 
 * Consumer sends this to producer to fetch historical data matching
 * a target timestamp. Producer searches its history buffer and returns
 * the closest match based on interpolation mode.
 * 
 * This is a one-shot request (not subscription). Reply goes to CMD mailbox.
 * 
 * @tparam T Data payload type (e.g., TemperatureData, SensorData)
 */
template<typename T>
struct GetDataRequestPayload {
    uint64_t target_timestamp{0};      ///< Target timestamp in nanoseconds
    uint64_t tolerance_ns{0};          ///< Max time difference allowed (0 = any)
    uint8_t interpolation_mode{0};     ///< 0=NEAREST, 1=LINEAR, 2=PREVIOUS, 3=NEXT
    
    // For CmdInput compatibility - reply type
    using ReplyType = GetDataReplyPayload<T>;
};

/**
 * @brief Reply to GetDataRequest with matching data (generic over data type)
 * 
 * Producer sends this back with the requested data. The reply itself is wrapped
 * in TimsMessage<GetDataReplyPayload<T>>, so this payload just contains the raw
 * data T plus metadata for SyncedInput to determine freshness.
 * 
 * @tparam T Data payload type (same as request)
 */
template<typename T>
struct GetDataReplyPayload {
    T data;                            ///< The matched data payload (not wrapped)
    bool found{false};                 ///< True if match found within tolerance
    uint64_t data_timestamp{0};        ///< Timestamp of the matched data
    int64_t timestamp_delta_ns{0};     ///< Actual time difference (signed)
};

/**
 * @brief Request next available data (no timestamp matching, generic over data type)
 * 
 * Consumer sends this to producer to fetch the next available message
 * from its history buffer. This is useful for one-shot data fetches
 * without continuous subscription.
 * 
 * This is a one-shot request. Reply goes to CMD mailbox.
 * 
 * @tparam T Data payload type
 */
template<typename T>
struct GetNextDataRequestPayload {
    // Empty - just request next available message
    
    // For CmdInput compatibility - reply type
    using ReplyType = GetNextDataReplyPayload<T>;
};

/**
 * @brief Reply to GetNextDataRequest with next available data (generic over data type)
 * 
 * Producer sends this back with the next available data. The reply itself is wrapped
 * in TimsMessage<GetNextDataReplyPayload<T>>, so this payload just contains the raw data.
 * 
 * @tparam T Data payload type (same as request)
 */
template<typename T>
struct GetNextDataReplyPayload {
    T data;                            ///< The next available data payload (not wrapped)
    bool found{false};                 ///< True if data available
    uint64_t data_timestamp{0};        ///< Timestamp of the data
};

// ============================================================================
// Message Definitions with Request-Reply Pairing (Generic Templates)
// ============================================================================

/**
 * @brief GetData request message template
 * 
 * Each data type T gets its own GetDataRequest<T, ID> message.
 * Uses the same local_id as the corresponding Message::Data<T> but with
 * UserSubPrefix::GetData instead of UserSubPrefix::Data.
 * 
 * Full message ID structure:
 *   Data message:     [UserDefined][Data][ID]        = 0x0100xxxx
 *   GetDataRequest:   [UserDefined][GetData][ID]     = 0x0103xxxx
 * 
 * This ensures perfect collision avoidance via subprefix field!
 * 
 * @tparam T Data payload type
 * @tparam ID Local message ID (same as data message, defaults to AUTO_ID)
 */
template<typename T, uint16_t ID = 0>
using GetDataRequest = MessageDefinition<
    GetDataRequestPayload<T>,
    MessagePrefix::UserDefined,        // User messages, not system
    UserSubPrefix::GetData,            // Dedicated GetData subprefix
    ID,                                // Same ID as data message (different SubPrefix = no collision)
    GetDataReplyPayload<T>             // Reply type (automatically gets ID = -ID)
>;

/**
 * @brief GetNextData request message template
 * 
 * Each data type T gets its own GetNextDataRequest<T, ID> message.
 * Uses the same local_id as the corresponding Message::Data<T> but with
 * UserSubPrefix::GetNextData instead of UserSubPrefix::Data.
 * 
 * Full message ID structure:
 *   Data message:         [UserDefined][Data][ID]        = 0x0100xxxx
 *   GetNextDataRequest:   [UserDefined][GetNextData][ID] = 0x0104xxxx
 * 
 * This ensures perfect collision avoidance via subprefix field!
 * 
 * @tparam T Data payload type
 * @tparam ID Local message ID (same as data message, defaults to AUTO_ID)
 */
template<typename T, uint16_t ID = 0>
using GetNextDataRequest = MessageDefinition<
    GetNextDataRequestPayload<T>,
    MessagePrefix::UserDefined,        // User messages, not system
    UserSubPrefix::GetNextData,        // Dedicated GetNextData subprefix
    ID,                                // Same ID as data message
    GetNextDataReplyPayload<T>         // Reply type (automatically gets ID = -ID)
>;

/**
 * @brief Reply message type aliases (automatically created via ReplyMessageDef)
 */
template<typename T, uint16_t ID = 0>
using GetDataReply = typename GetDataRequest<T, ID>::ReplyMessageDef;

template<typename T, uint16_t ID = 0>
using GetNextDataReply = typename GetNextDataRequest<T, ID>::ReplyMessageDef;

} // namespace commrat
