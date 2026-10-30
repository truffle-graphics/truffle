file(MAKE_DIRECTORY "${TRUFFLE_SHADERC_TEST_DIR}")
set(_package "${TRUFFLE_SHADERC_TEST_DIR}/modules.tsp")
set(_args --compile --name slang-modules --source-language slang --target spirv
    --stage compute --entry computeMain --define QUALITY=3
    --include-root "${TRUFFLE_SLANG_FIXTURES}"
    --input "${TRUFFLE_SLANG_FIXTURES}/main.slang" --output "${_package}")
execute_process(COMMAND "${TRUFFLE_SHADERC}" ${_args} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${TRUFFLE_SLANG_TEST}" "${_package}" COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${_package}" _good)
execute_process(COMMAND "${TRUFFLE_SHADERC}" ${_args} --entry missingMain
    RESULT_VARIABLE _failed ERROR_VARIABLE _diagnostics)
if(_failed EQUAL 0 OR NOT _diagnostics MATCHES "Slang entry point failed")
    message(FATAL_ERROR "Missing Slang entry did not fail truthfully: ${_diagnostics}")
endif()
file(SHA256 "${_package}" _retained)
if(NOT _retained STREQUAL _good)
    message(FATAL_ERROR "Failed Slang compile replaced the prepared package")
endif()
execute_process(COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _inspection)
if(NOT _result EQUAL 0 OR NOT _inspection MATCHES "compiler=slang version=2026.12.1")
    message(FATAL_ERROR "Slang compiler provenance missing: ${_inspection}")
endif()
