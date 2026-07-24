#pragma once
/**
 * @file all_examples_app.hpp
 * @brief Unified CommRaT registry covering every example message type.
 *
 * Use this when launching multiple CommRaT examples together (e.g. via a
 * single ratgui session or a combined launcher config).  All participating
 * binaries must include this header so message IDs are identical.
 *
 * WARNING: The type order below is FIXED.  Adding a type in the middle or
 * removing one changes the IDs of all subsequent types and breaks binary
 * compatibility.  New types must always be appended at the end.
 *
 * Current ID assignments:
 *   1  example_messages::StatusData
 *   2  example_messages::CounterData
 *   3  example_messages::TemperatureData
 *   4  example_messages::PoseData
 *   5  sensor_filter_example::SensorData
 *   6  sensor_filter_example::FilteredData
 *   7  imu_gps_example::IMUData
 *   8  imu_gps_example::GPSData
 *   9  imu_gps_example::FusedPose
 */

#include <commrat/examples/common_messages.hpp>
#include <commrat/examples/sensor_filter_messages.hpp>
#include <commrat/examples/imu_gps_messages.hpp>

// AllExamplesApp — combines every example message type in a single registry.
// Usable anywhere CommRaT<> is used; the registry is a superset of every
// individual sub-app (ExampleApp, SensorFilterApp, ImuGpsApp).
using AllExamplesApp = commrat::CommRaT<
    commrat::Message::Data<example_messages::StatusData>,               // ID 1
    commrat::Message::Data<example_messages::CounterData>,              // ID 2
    commrat::Message::Data<example_messages::TemperatureData>,          // ID 3
    commrat::Message::Data<example_messages::PoseData>,                 // ID 4
    commrat::Message::Data<sensor_filter_example::SensorData>,          // ID 5
    commrat::Message::Data<sensor_filter_example::FilteredData>,        // ID 6
    commrat::Message::Data<imu_gps_example::IMUData>,                   // ID 7
    commrat::Message::Data<imu_gps_example::GPSData>,                   // ID 8
    commrat::Message::Data<imu_gps_example::FusedPose>                  // ID 9
>;
