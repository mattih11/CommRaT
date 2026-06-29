# Tests use assert() for checks - ensure it is never compiled out regardless of build type.
add_compile_options(-UNDEBUG)

add_executable(test_process_signature test_process_signature.cpp)
target_link_libraries(test_process_signature PRIVATE commrat)
add_test(NAME test_process_signature COMMAND test_process_signature)

add_executable(test_multi_output test_multi_output.cpp)
target_link_libraries(test_multi_output PRIVATE commrat)
add_test(NAME test_multi_output COMMAND test_multi_output)

add_executable(test_auto_id_validation test_auto_id_validation.cpp)
target_link_libraries(test_auto_id_validation PRIVATE commrat)
add_test(NAME test_auto_id_validation COMMAND test_auto_id_validation)

add_executable(test_synced_wrapper test_synced_wrapper.cpp)
target_link_libraries(test_synced_wrapper PRIVATE commrat)
add_test(NAME test_synced_wrapper COMMAND test_synced_wrapper)

add_executable(test_workmailbox test_workmailbox.cpp)
target_link_libraries(test_workmailbox PRIVATE commrat)
add_test(NAME test_workmailbox COMMAND test_workmailbox)

add_executable(test_system_messages test_system_messages.cpp)
target_link_libraries(test_system_messages PRIVATE commrat)
add_test(NAME test_system_messages COMMAND test_system_messages)

add_executable(test_registry_utils test_registry_utils.cpp)
target_link_libraries(test_registry_utils PRIVATE commrat)
add_test(NAME test_registry_utils COMMAND test_registry_utils)

add_executable(test_timestamped_ring_buffer test_timestamped_ring_buffer.cpp)
target_link_libraries(test_timestamped_ring_buffer PRIVATE commrat)
add_test(NAME test_timestamped_ring_buffer COMMAND test_timestamped_ring_buffer)

add_executable(test_historical_mailbox test_historical_mailbox.cpp)
target_link_libraries(test_historical_mailbox PRIVATE commrat)
add_test(NAME test_historical_mailbox COMMAND test_historical_mailbox)

add_executable(test_multi_input test_multi_input.cpp)
target_link_libraries(test_multi_input PRIVATE commrat)
add_test(NAME test_multi_input COMMAND test_multi_input)

add_executable(test_multi_input_module test_multi_input_module.cpp)
target_link_libraries(test_multi_input_module PRIVATE commrat)
add_test(NAME test_multi_input_module COMMAND test_multi_input_module)

add_executable(test_multi_input_compilation test_multi_input_compilation.cpp)
target_link_libraries(test_multi_input_compilation PRIVATE commrat)
add_test(NAME test_multi_input_compilation COMMAND test_multi_input_compilation)

add_executable(test_3input_fusion test_3input_fusion.cpp)
target_link_libraries(test_3input_fusion PRIVATE commrat)
add_test(NAME test_3input_fusion COMMAND test_3input_fusion)

add_executable(test_timestamp_logic test_timestamp_logic.cpp)
target_link_libraries(test_timestamp_logic PRIVATE commrat)
add_test(NAME test_timestamp_logic COMMAND test_timestamp_logic)

add_executable(test_mailbox_sizing test_mailbox_sizing.cpp)
target_link_libraries(test_mailbox_sizing PRIVATE commrat)
add_test(NAME test_mailbox_sizing COMMAND test_mailbox_sizing)

add_executable(test_typed_mailbox test_typed_mailbox.cpp)
target_link_libraries(test_typed_mailbox PRIVATE commrat)
add_test(NAME test_typed_mailbox COMMAND test_typed_mailbox)

add_executable(test_type_name test_type_name.cpp)
target_link_libraries(test_type_name PRIVATE commrat)
add_test(NAME test_type_name COMMAND test_type_name)

add_executable(test_address_collisions test_address_collisions.cpp)
target_link_libraries(test_address_collisions PRIVATE commrat)
add_test(NAME test_address_collisions COMMAND test_address_collisions)
set_tests_properties(test_address_collisions PROPERTIES TIMEOUT 45)

add_executable(test_getdata_registration test_getdata_registration.cpp)
target_link_libraries(test_getdata_registration PRIVATE commrat)
add_test(NAME test_getdata_registration COMMAND test_getdata_registration)

add_executable(test_module_output_getdata test_module_output_getdata.cpp)
target_link_libraries(test_module_output_getdata PRIVATE commrat)
add_test(NAME test_module_output_getdata COMMAND test_module_output_getdata)

add_executable(test_subscription_protocol test_subscription_protocol.cpp)
target_link_libraries(test_subscription_protocol PRIVATE commrat)
add_test(NAME test_subscription_protocol COMMAND test_subscription_protocol)

# Example run tests (only if examples were built)
if(COMMRAT_BUILD_EXAMPLES)
    add_test(NAME example_continuous_input
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_continuous_example.sh
                $<TARGET_FILE:example_continuous_input>)
    add_test(NAME example_clean_interface COMMAND example_clean_interface)
    add_test(NAME example_commands COMMAND example_commands)
    set_tests_properties(example_commands PROPERTIES TIMEOUT 5)
    add_test(NAME example_loop_mode
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_continuous_example.sh
                $<TARGET_FILE:example_loop_mode>)
    add_test(NAME example_multi_output_runtime
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_continuous_example.sh
                $<TARGET_FILE:example_multi_output_runtime>)
    add_test(NAME example_sensor_fusion
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_continuous_example.sh
                $<TARGET_FILE:example_sensor_fusion>)

    add_test(NAME doc_example_hello_commrat COMMAND doc_example_hello_commrat)
    add_test(NAME doc_example_multi_output
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_continuous_example.sh
                $<TARGET_FILE:doc_example_multi_output>)
    add_test(NAME doc_example_multi_input_fusion
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_continuous_example.sh
                $<TARGET_FILE:doc_example_multi_input_fusion>)

    add_test(NAME module_main_basic
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_module_main_example.sh
                $<TARGET_FILE:module_main_basic>)
    add_test(NAME module_main_with_config
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_module_main_example.sh
                $<TARGET_FILE:module_main_with_config>)
    add_test(NAME module_main_multiformat
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_module_main_example.sh
                $<TARGET_FILE:module_main_multiformat>)
endif()
