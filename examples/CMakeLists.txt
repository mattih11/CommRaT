add_executable(example_continuous_input continuous_input_example.cpp)
target_link_libraries(example_continuous_input PRIVATE commrat)

add_executable(example_clean_interface clean_interface_example.cpp)
target_link_libraries(example_clean_interface PRIVATE commrat)

add_executable(example_commands command_example.cpp)
target_link_libraries(example_commands PRIVATE commrat)

add_executable(example_loop_mode loop_mode_example.cpp)
target_link_libraries(example_loop_mode PRIVATE commrat)

add_executable(example_multi_output_runtime multi_output_runtime.cpp)
target_link_libraries(example_multi_output_runtime PRIVATE commrat)

# Multi-output GUI producer — designed for use with RaTGUI.
# Publishes StatusData, CounterData, TemperatureData and PoseData at 10 Hz.
commrat_module(example_gui_producer
    SOURCES example_gui_producer.cpp
    MODULE_CLASS ExampleGUIProducerModule)

add_executable(example_sensor_fusion multi_output_sensor_fusion.cpp)
target_link_libraries(example_sensor_fusion PRIVATE commrat)

add_executable(introspection_example introspection_example.cpp)
target_link_libraries(introspection_example PRIVATE commrat)

# Standalone module main wrapper examples
# commrat_module() creates the executable and generates a *.module.json
# descriptor alongside the binary, used by ProcessLauncher for discovery.
commrat_module(module_main_basic
    SOURCES module_main_basic.cpp
    MODULE_CLASS BasicSensorModule)

commrat_module(module_main_with_config
    SOURCES module_main_with_config.cpp
    MODULE_CLASS FilterModule)

commrat_module(module_main_multiformat
    SOURCES module_main_multiformat.cpp
    MODULE_CLASS FusionModule)

# IMU + GPS fusion example system (separate producer binaries + process launcher)
commrat_module(imu_module
    SOURCES imu_module.cpp
    MODULE_CLASS IMUModule)

commrat_module(gps_module
    SOURCES gps_module.cpp
    MODULE_CLASS GPSModule)

add_executable(imu_gps_fusion_launcher imu_gps_fusion_launcher.cpp)
target_link_libraries(imu_gps_fusion_launcher PRIVATE commrat)

# Process launcher binary — generic; no module class of its own
add_executable(sensor_filter_launcher sensor_filter_launcher.cpp)
target_link_libraries(sensor_filter_launcher PRIVATE commrat)

# Documentation examples
add_executable(doc_example_hello_commrat
    ${CMAKE_SOURCE_DIR}/docs/examples/01_hello_commrat/hello_commrat.cpp)
target_link_libraries(doc_example_hello_commrat PRIVATE commrat)

add_executable(doc_example_multi_output
    ${CMAKE_SOURCE_DIR}/docs/examples/02_multi_output/multi_output_example.cpp)
target_link_libraries(doc_example_multi_output PRIVATE commrat)

add_executable(doc_example_multi_input_fusion
    ${CMAKE_SOURCE_DIR}/docs/examples/03_multi_input_fusion/multi_input_fusion.cpp)
target_link_libraries(doc_example_multi_input_fusion PRIVATE commrat)
