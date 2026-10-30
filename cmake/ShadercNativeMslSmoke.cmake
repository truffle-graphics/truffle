if(NOT EXISTS "${TRUFFLE_SHADERC_TEST_DIR}/interface.tsp")
    message(FATAL_ERROR "Native MSL interface package fixture was not produced")
endif()

function(run_msl_cli target suffix)
    set(output "${TRUFFLE_SHADERC_TEST_DIR}/cli-${suffix}.tsp")
    execute_process(
        COMMAND "${TRUFFLE_SHADERC}"
            --name shaderc-native-msl
            --target "${target}"
            --stage vertex
            --source-language msl
            --input "${TRUFFLE_NATIVE_MSL_FIXTURE_DIR}/root.metal"
            --output "${output}"
            --entry vertex_main
            --compile
            --source-unit common.metal
            --include-root "${TRUFFLE_NATIVE_MSL_FIXTURE_DIR}"
            --define TRUFFLE_MSL_CONFIG=1
            --interface-from "${TRUFFLE_SHADERC_TEST_DIR}/interface.tsp"
            --msl-version 2.1
            --allow-experimental-metal
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "shaderc native MSL ${target} failed (${result}):\n${stdout}\n${stderr}")
    endif()
    if(NOT EXISTS "${output}")
        message(FATAL_ERROR "shaderc did not write ${target} output")
    endif()
endfunction()

run_msl_cli(msl source)
if(TRUFFLE_EXPECT_METALLIB)
    run_msl_cli(metallib library)
endif()
