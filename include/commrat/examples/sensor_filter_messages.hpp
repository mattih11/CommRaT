#pragma once
/**
 * @file sensor_filter_messages.hpp
 * @brief Shared message types for the sensor + filter example pair.
 *
 * Include this in every binary that participates in a sensor/filter
 * application so that message IDs are identical across processes.
 *
 * For running ALL examples together, prefer all_examples_app.hpp which
 * combines these types with every other example registry.
 */

#include <commrat/commrat.hpp>
#include <cstdint>

namespace sensor_filter_example {

// ============================================================================
// Payload definitions (POD)
// ============================================================================

struct SensorData {
    uint64_t sensor_id{0};
    float    temperature_c{0.0f};
    float    humidity_percent{0.0f};
};

struct FilteredData {
    uint64_t sensor_id{0};
    float    smoothed_temperature_c{0.0f};
    float    smoothed_humidity_percent{0.0f};
    uint32_t sample_count{0};
};

// ============================================================================
// Application registry (standalone usage)
// ============================================================================

using SensorFilterApp = commrat::CommRaT<
    commrat::Message::Data<SensorData>,
    commrat::Message::Data<FilteredData>
>;

} // namespace sensor_filter_example
