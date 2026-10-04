if(NOT DEFINED RUNTIME OR NOT DEFINED NM OR NOT DEFINED TEST_OUTPUT)
    message(FATAL_ERROR "RUNTIME, NM, and TEST_OUTPUT are required")
endif()

file(REMOVE "${TEST_OUTPUT}" "${TEST_OUTPUT}.tmp")
execute_process(
    COMMAND "${RUNTIME}"
        --commrat-inspect
        "${TEST_OUTPUT}"
        RuntimeMustNotInspect
        "${RUNTIME}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(result EQUAL 0)
    message(FATAL_ERROR "Runtime module accepted the descriptor inspection command")
endif()
if(EXISTS "${TEST_OUTPUT}" OR EXISTS "${TEST_OUTPUT}.tmp")
    message(FATAL_ERROR "Runtime module wrote descriptor output")
endif()

execute_process(
    COMMAND "${NM}" -C "${RUNTIME}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect runtime symbols: ${nm_error}")
endif()
if(symbols MATCHES "write_module_inspect|descriptor_inspector_main")
    message(FATAL_ERROR "Runtime module contains descriptor-generation symbols")
endif()
