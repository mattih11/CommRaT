foreach(_required_variable IN ITEMS
        COMMRAT_INSPECT_EXECUTABLE
        COMMRAT_DESCRIPTOR
        COMMRAT_MODULE_CLASS
        COMMRAT_MODULE_BINARY)
    if(NOT DEFINED ${_required_variable})
        message(FATAL_ERROR "Module inspection requires ${_required_variable}")
    endif()
endforeach()

file(REMOVE "${COMMRAT_DESCRIPTOR}" "${COMMRAT_DESCRIPTOR}.tmp")

execute_process(
    COMMAND "${COMMRAT_INSPECT_EXECUTABLE}"
        --commrat-inspect
        "${COMMRAT_DESCRIPTOR}"
        "${COMMRAT_MODULE_CLASS}"
        "${COMMRAT_MODULE_BINARY}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)

if(NOT "${_result}" STREQUAL "0")
    file(REMOVE "${COMMRAT_DESCRIPTOR}" "${COMMRAT_DESCRIPTOR}.tmp")
    message(FATAL_ERROR
        "Inspection failed for module '${COMMRAT_MODULE_CLASS}' (exit ${_result}).\n"
        "stdout:\n${_stdout}\n"
        "stderr:\n${_stderr}")
endif()

if(NOT EXISTS "${COMMRAT_DESCRIPTOR}")
    message(FATAL_ERROR
        "Inspection for module '${COMMRAT_MODULE_CLASS}' exited successfully but did not produce "
        "'${COMMRAT_DESCRIPTOR}'.\nstdout:\n${_stdout}\nstderr:\n${_stderr}")
endif()

file(READ "${COMMRAT_DESCRIPTOR}" _descriptor_json)
string(JSON _root_type ERROR_VARIABLE _json_error TYPE "${_descriptor_json}")
if(NOT "${_json_error}" STREQUAL "NOTFOUND" OR NOT "${_root_type}" STREQUAL "OBJECT")
    file(REMOVE "${COMMRAT_DESCRIPTOR}" "${COMMRAT_DESCRIPTOR}.tmp")
    message(FATAL_ERROR
        "Inspection for module '${COMMRAT_MODULE_CLASS}' produced invalid JSON: ${_json_error}")
endif()

foreach(_field IN ITEMS module_class binary outputs inputs synced_inputs execution_mode)
    string(JSON _field_type ERROR_VARIABLE _field_error TYPE "${_descriptor_json}" "${_field}")
    if(NOT "${_field_error}" STREQUAL "NOTFOUND")
        file(REMOVE "${COMMRAT_DESCRIPTOR}" "${COMMRAT_DESCRIPTOR}.tmp")
        message(FATAL_ERROR
            "Inspection for module '${COMMRAT_MODULE_CLASS}' produced an incomplete descriptor: "
            "${_field_error}")
    endif()
endforeach()

string(JSON _actual_module_class GET "${_descriptor_json}" module_class)
if(NOT "${_actual_module_class}" STREQUAL "${COMMRAT_MODULE_CLASS}")
    file(REMOVE "${COMMRAT_DESCRIPTOR}" "${COMMRAT_DESCRIPTOR}.tmp")
    message(FATAL_ERROR
        "Inspection produced module_class '${_actual_module_class}', expected '${COMMRAT_MODULE_CLASS}'")
endif()