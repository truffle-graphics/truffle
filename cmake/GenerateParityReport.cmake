if(NOT DEFINED TRUFFLE_BUILD_DIR)
    message(FATAL_ERROR "TRUFFLE_BUILD_DIR is required")
endif()

if(NOT DEFINED TRUFFLE_REPORT_OUT)
    message(FATAL_ERROR "TRUFFLE_REPORT_OUT is required")
endif()

set(_tracked_tests
    truffle_rhi_contract_tests
    truffle_vulkan_tests
    truffle_opengl_tests
    truffle_direct3d_tests
    truffle_metal_tests
)

execute_process(
    COMMAND ctest --test-dir "${TRUFFLE_BUILD_DIR}" -N
    RESULT_VARIABLE _list_result
    OUTPUT_VARIABLE _list_output
    ERROR_VARIABLE _list_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT _list_result EQUAL 0)
    message(FATAL_ERROR "Failed to list tests in ${TRUFFLE_BUILD_DIR}: ${_list_error}")
endif()

set(_report "# Backend Parity Matrix\n\n")
string(APPEND _report "- Build directory: ${TRUFFLE_BUILD_DIR}\n")
string(TIMESTAMP _ts UTC)
string(APPEND _report "- Generated (UTC): ${_ts}\n\n")
string(APPEND _report "| Test | Status | Notes |\n")
string(APPEND _report "|---|---|---|\n")

foreach(_test IN LISTS _tracked_tests)
    string(FIND "${_list_output}" "${_test}" _test_found)
    if(_test_found EQUAL -1)
        string(APPEND _report "| ${_test} | not-built | test not present in current preset/build |\n")
        continue()
    endif()

    execute_process(
        COMMAND ctest --test-dir "${TRUFFLE_BUILD_DIR}" -R "^${_test}$" --output-on-failure
        RESULT_VARIABLE _run_result
        OUTPUT_VARIABLE _run_output
        ERROR_VARIABLE _run_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(_run_result EQUAL 0)
        string(APPEND _report "| ${_test} | pass | executed successfully |\n")
    else()
        string(APPEND _report "| ${_test} | fail | see CI logs for output |\n")
        file(MAKE_DIRECTORY "${TRUFFLE_BUILD_DIR}")
        file(WRITE "${TRUFFLE_BUILD_DIR}/${_test}.parity.log"
            "--- ctest stdout ---\n${_run_output}\n\n--- ctest stderr ---\n${_run_error}\n")
        file(WRITE "${TRUFFLE_REPORT_OUT}" "${_report}")
        message(FATAL_ERROR "Parity test ${_test} failed")
    endif()
endforeach()

get_filename_component(_report_dir "${TRUFFLE_REPORT_OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_report_dir}")
file(WRITE "${TRUFFLE_REPORT_OUT}" "${_report}")

message(STATUS "Wrote backend parity report to ${TRUFFLE_REPORT_OUT}")
