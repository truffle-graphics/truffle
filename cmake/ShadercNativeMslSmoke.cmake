if(NOT EXISTS "${TRUFFLE_SHADERC_TEST_DIR}/interface.tsp")
    message(FATAL_ERROR "Native MSL interface package fixture was not produced")
endif()

function(run_msl_cli target suffix interfacePackage shouldSucceed)
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
            --interface-from "${TRUFFLE_SHADERC_TEST_DIR}/${interfacePackage}"
            --msl-version 2.1
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(shouldSucceed AND NOT result EQUAL 0)
        message(FATAL_ERROR
            "shaderc native MSL ${target} failed (${result}):\n${stdout}\n${stderr}")
    endif()
    if(NOT shouldSucceed AND result EQUAL 0)
        message(FATAL_ERROR
            "shaderc accepted ambiguous interface declarations:\n${stdout}\n${stderr}")
    endif()
    if(shouldSucceed AND NOT EXISTS "${output}")
        message(FATAL_ERROR "shaderc did not write ${target} output")
    endif()
    if(NOT shouldSucceed AND
       NOT stderr MATCHES "matching stage/entry variants with differing interface declarations")
        message(FATAL_ERROR
            "shaderc ambiguity error was not actionable:\n${stdout}\n${stderr}")
    endif()
endfunction()

run_msl_cli(msl source interface.tsp TRUE)
run_msl_cli(msl agreeing interface-agreeing.tsp TRUE)
run_msl_cli(msl differing interface-differing.tsp FALSE)
if(TRUFFLE_EXPECT_METALLIB)
    run_msl_cli(metallib library interface.tsp TRUE)
endif()
