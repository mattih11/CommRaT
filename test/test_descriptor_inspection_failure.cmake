if(NOT DEFINED RUNNER OR NOT DEFINED TEST_OUTPUT)
    message(FATAL_ERROR "RUNNER and TEST_OUTPUT are required")
endif()

file(WRITE "${TEST_OUTPUT}" "{\"module_class\":\"stale\"}\n")

set(_project_dir "${CMAKE_CURRENT_BINARY_DIR}/descriptor-failure-project")
set(_build_dir "${_project_dir}/build")
file(REMOVE_RECURSE "${_project_dir}")
file(MAKE_DIRECTORY "${_project_dir}")
file(WRITE "${_project_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.21)
project(descriptor_failure NONE)
add_custom_target(failing_module ALL
    COMMAND \"${CMAKE_COMMAND}\"
        \"-DCOMMRAT_INSPECT_EXECUTABLE=${CMAKE_COMMAND}\"
        \"-DCOMMRAT_DESCRIPTOR=${TEST_OUTPUT}\"
        \"-DCOMMRAT_MODULE_CLASS=FailingInspector\"
        \"-DCOMMRAT_MODULE_BINARY=/tmp/failing-inspector\"
        -P \"${RUNNER}\"
    VERBATIM)
")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_project_dir}" -B "${_build_dir}"
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_stdout
    ERROR_VARIABLE _configure_stderr
)
if(NOT "${_configure_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Could not configure failing-inspector test project:\n"
        "${_configure_stdout}\n${_configure_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)

if("${_result}" STREQUAL "0")
    message(FATAL_ERROR "Failing inspector build unexpectedly succeeded")
endif()
if(EXISTS "${TEST_OUTPUT}" OR EXISTS "${TEST_OUTPUT}.tmp")
    message(FATAL_ERROR "Failing inspector left a stale or temporary descriptor")
endif()

set(_diagnostics "${_stdout}\n${_stderr}")
string(FIND "${_diagnostics}" "Inspection failed for module 'FailingInspector'" _message_index)
if(_message_index EQUAL -1)
    message(FATAL_ERROR "Failure diagnostics were not actionable:\n${_diagnostics}")
endif()