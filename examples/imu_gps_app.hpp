#pragma once
/**
 * @file imu_gps_app.hpp
 * @brief Backward-compatible include for the IMU + GPS fusion example.
 *
 * Previously defined IMUData, GPSData, FusedPose, and ImuGpsApp inline.
 * Those types are now in the installed header
 * <commrat/examples/imu_gps_messages.hpp> under namespace imu_gps_example,
 * and ImuGpsApp is aliased to AllExamplesApp so that all example binaries
 * share a single unified registry when run together.
 */

#include <commrat/examples/all_examples_app.hpp>

// Bring IMU/GPS types into the local scope (backward compat with
// existing code that uses IMUData, GPSData, FusedPose unqualified).
using imu_gps_example::IMUData;
using imu_gps_example::GPSData;
using imu_gps_example::FusedPose;

// ImuGpsApp is now the full AllExamplesApp registry so that imu/gps
// modules are observable alongside every other example from ratgui.
using ImuGpsApp = AllExamplesApp;
