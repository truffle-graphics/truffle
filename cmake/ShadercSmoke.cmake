if(NOT DEFINED TRUFFLE_SHADERC OR NOT DEFINED TRUFFLE_SHADERC_TEST_DIR)
    message(FATAL_ERROR "ShadercSmoke requires tool and output paths")
endif()

file(MAKE_DIRECTORY "${TRUFFLE_SHADERC_TEST_DIR}")
set(_input "${TRUFFLE_SHADERC_TEST_DIR}/fixture.spv")
set(_dxil_input "${TRUFFLE_SHADERC_TEST_DIR}/fixture.dxil")
set(_package "${TRUFFLE_SHADERC_TEST_DIR}/fixture.truffle-shader")
file(WRITE "${_input}" "SPVFIXTURE")
file(WRITE "${_dxil_input}" "DXILFIXTURE")

execute_process(
    COMMAND
        "${TRUFFLE_SHADERC}"
        --name smoke
        --target spirv
        --stage compute
        --input "${_input}"
        --output "${_package}"
        --source-hash
            0000000000000000000000000000000000000000000000000000000000000000
        --source-language spirv
        --compiler-name fixture
        --compiler-version 1
    RESULT_VARIABLE _assemble_result
    OUTPUT_VARIABLE _assemble_output
    ERROR_VARIABLE _assemble_error)
if(NOT _assemble_result EQUAL 0)
    message(FATAL_ERROR
        "truffle-shaderc assembly failed: ${_assemble_output}${_assemble_error}")
endif()

execute_process(
    COMMAND
        "${TRUFFLE_SHADERC}"
        --name smoke
        --target dxil
        --stage compute
        --input "${_dxil_input}"
        --output "${_package}"
        --append "${_package}"
        --source-hash
            1111111111111111111111111111111111111111111111111111111111111111
        --source-language dxil
        --compiler-name fixture
        --compiler-version 1
    RESULT_VARIABLE _append_result
    OUTPUT_VARIABLE _append_output
    ERROR_VARIABLE _append_error)
if(NOT _append_result EQUAL 0)
    message(FATAL_ERROR
        "truffle-shaderc append failed: ${_append_output}${_append_error}")
endif()

execute_process(
    COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package}"
    RESULT_VARIABLE _inspect_result
    OUTPUT_VARIABLE _inspect_output
    ERROR_VARIABLE _inspect_error)
if(NOT _inspect_result EQUAL 0 OR
   NOT _inspect_output MATCHES "smoke variants=2")
    message(FATAL_ERROR
        "truffle-shaderc inspection failed: ${_inspect_output}${_inspect_error}")
endif()
