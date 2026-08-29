if(NOT DEFINED TRUFFLE_BUILD_DIR)
    message(FATAL_ERROR "TRUFFLE_BUILD_DIR is required")
endif()

if(NOT DEFINED TRUFFLE_REPORT_OUT)
    message(FATAL_ERROR "TRUFFLE_REPORT_OUT is required")
endif()

if(NOT DEFINED TRUFFLE_REPORT_JSON_OUT)
    string(REGEX REPLACE "\\.[^.]*$" ".json" TRUFFLE_REPORT_JSON_OUT "${TRUFFLE_REPORT_OUT}")
endif()

if(NOT DEFINED TRUFFLE_RHI_PARITY_JSON_OUT)
    set(TRUFFLE_RHI_PARITY_JSON_OUT "${TRUFFLE_BUILD_DIR}/rhi-parity-report.json")
endif()

function(_truffle_json_escape out input)
    string(REPLACE "\\" "\\\\" _escaped "${input}")
    string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
    string(REPLACE "\n" "\\n" _escaped "${_escaped}")
    set(${out} "${_escaped}" PARENT_SCOPE)
endfunction()

set(_tracked_tests
    truffle_rhi_contract_tests
    truffle_vulkan_tests
    truffle_opengl_tests
    truffle_opengles_tests
    truffle_direct3d_tests
    truffle_metal_tests
    truffle_metal_presentation_tests
    truffle_webgpu_tests
    truffle_webgl2_tests
    truffle_backend_support_tests
)

get_filename_component(_report_dir "${TRUFFLE_REPORT_OUT}" DIRECTORY)
get_filename_component(_json_report_dir "${TRUFFLE_REPORT_JSON_OUT}" DIRECTORY)
get_filename_component(_rhi_json_report_dir "${TRUFFLE_RHI_PARITY_JSON_OUT}" DIRECTORY)
if(_report_dir)
    file(MAKE_DIRECTORY "${_report_dir}")
endif()
if(_json_report_dir)
    file(MAKE_DIRECTORY "${_json_report_dir}")
endif()
if(_rhi_json_report_dir)
    file(MAKE_DIRECTORY "${_rhi_json_report_dir}")
endif()

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

_truffle_json_escape(_json_build_dir "${TRUFFLE_BUILD_DIR}")
_truffle_json_escape(_json_generated "${_ts}")
set(_json "{\n")
string(APPEND _json "  \"buildDirectory\": \"${_json_build_dir}\",\n")
string(APPEND _json "  \"generatedUtc\": \"${_json_generated}\",\n")
string(APPEND _json "  \"tests\": [\n")
set(_json_first_test TRUE)

function(_truffle_append_json_test name status notes)
    _truffle_json_escape(_json_name "${name}")
    _truffle_json_escape(_json_status "${status}")
    _truffle_json_escape(_json_notes "${notes}")
    if(_json_first_test)
        set(_entry_prefix "    ")
        set(_json_first_test FALSE PARENT_SCOPE)
    else()
        set(_entry_prefix ",\n    ")
    endif()
    string(APPEND _json
        "${_entry_prefix}{\"test\": \"${_json_name}\", \"status\": \"${_json_status}\", \"notes\": \"${_json_notes}\"}")
    set(_json "${_json}" PARENT_SCOPE)
endfunction()

foreach(_test IN LISTS _tracked_tests)
    string(FIND "${_list_output}" "${_test}" _test_found)
    if(_test_found EQUAL -1)
        string(APPEND _report "| ${_test} | not-built | test not present in current preset/build |\n")
        _truffle_append_json_test("${_test}" "not-built" "test not present in current preset/build")
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
        _truffle_append_json_test("${_test}" "pass" "executed successfully")
    else()
        string(APPEND _report "| ${_test} | fail | see CI logs for output |\n")
        _truffle_append_json_test("${_test}" "fail" "see CI logs for output")
        file(MAKE_DIRECTORY "${TRUFFLE_BUILD_DIR}")
        file(WRITE "${TRUFFLE_BUILD_DIR}/${_test}.parity.log"
            "--- ctest stdout ---\n${_run_output}\n\n--- ctest stderr ---\n${_run_error}\n")
        file(WRITE "${TRUFFLE_REPORT_OUT}" "${_report}")
        string(APPEND _json "\n  ]\n}\n")
        file(WRITE "${TRUFFLE_REPORT_JSON_OUT}" "${_json}")
        message(FATAL_ERROR "Parity test ${_test} failed")
    endif()
endforeach()

file(WRITE "${TRUFFLE_REPORT_OUT}" "${_report}")
string(APPEND _json "\n  ]\n}\n")
file(WRITE "${TRUFFLE_REPORT_JSON_OUT}" "${_json}")

if(CMAKE_HOST_WIN32)
    set(_truffle_exe_suffix ".exe")
else()
    set(_truffle_exe_suffix "")
endif()

set(_rhi_parity_report_exe
    "${TRUFFLE_BUILD_DIR}/tests/truffle_rhi_parity_report${_truffle_exe_suffix}")
if(EXISTS "${_rhi_parity_report_exe}")
    execute_process(
        COMMAND "${_rhi_parity_report_exe}" "${TRUFFLE_RHI_PARITY_JSON_OUT}"
        RESULT_VARIABLE _rhi_report_result
        OUTPUT_VARIABLE _rhi_report_output
        ERROR_VARIABLE _rhi_report_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _rhi_report_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to write live RHI parity report with ${_rhi_parity_report_exe}: "
            "${_rhi_report_error}")
    endif()
    message(STATUS "Wrote live RHI parity JSON to ${TRUFFLE_RHI_PARITY_JSON_OUT}")
else()
    message(STATUS "Live RHI parity reporter not built in ${TRUFFLE_BUILD_DIR}")
endif()

message(STATUS "Wrote backend parity report to ${TRUFFLE_REPORT_OUT}")
message(STATUS "Wrote backend parity JSON to ${TRUFFLE_REPORT_JSON_OUT}")
