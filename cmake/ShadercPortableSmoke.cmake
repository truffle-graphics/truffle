file(MAKE_DIRECTORY "${TRUFFLE_SHADERC_TEST_DIR}")
set(_source "${TRUFFLE_SHADERC_TEST_DIR}/portable.glsl")
set(_package "${TRUFFLE_SHADERC_TEST_DIR}/portable.tsp")
execute_process(COMMAND "${TRUFFLE_SHADERC}" --routes OUTPUT_VARIABLE _routes
    COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" _lines "${_routes}")
set(_count 0)
foreach(_line IN LISTS _lines)
    if(NOT _line MATCHES "^source=(glsl|glsl-es) target=(msl|glsl|glsl-es) stage=(vertex|fragment|compute) vulkan=(1\\.[01]) spirv=(1\\.[03])")
        continue()
    endif()
    set(_language "${CMAKE_MATCH_1}")
    set(_target "${CMAKE_MATCH_2}")
    set(_stage "${CMAKE_MATCH_3}")
    set(_vulkan "${CMAKE_MATCH_4}")
    set(_spirv "${CMAKE_MATCH_5}")
    execute_process(COMMAND "${TRUFFLE_SHADER_EQUIVALENCE}" "${_source}"
        "${_target}" "${_stage}" "${_language}" "${_vulkan}" "${_spirv}" "${_package}" write
        COMMAND_ERROR_IS_FATAL ANY)
    set(_native_option --msl-version 2.1)
    if(_target STREQUAL "glsl")
        set(_native_option --glsl-version 410)
    elseif(_target STREQUAL "glsl-es")
        set(_native_option --glsl-es-version 300)
    endif()
    execute_process(COMMAND "${TRUFFLE_SHADERC}" --compile --name portable
        --target "${_target}" --stage "${_stage}" --source-language "${_language}"
        --input "${_source}" --output "${_package}" --allow-experimental-metal
        --vulkan-version "${_vulkan}" --spirv-version "${_spirv}" ${_native_option}
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${TRUFFLE_SHADER_EQUIVALENCE}" "${_source}"
        "${_target}" "${_stage}" "${_language}" "${_vulkan}" "${_spirv}" "${_package}" compare
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package}" --allow-experimental-metal
        RESULT_VARIABLE _result OUTPUT_VARIABLE _inspection)
    if(NOT _result EQUAL 0 OR NOT _inspection MATCHES "native-entry=" OR
       NOT _inspection MATCHES "spirv-cross=" OR NOT _inspection MATCHES "spirv-tools=")
        message(FATAL_ERROR "Missing serialized native compiler provenance")
    endif()
    if(_stage STREQUAL "fragment" AND NOT _inspection MATCHES "remap=1:6")
        message(FATAL_ERROR "Sampler remap was dropped: ${_inspection}")
    endif()
    if(_target STREQUAL "msl")
        execute_process(COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package}"
            RESULT_VARIABLE _unsafe OUTPUT_QUIET ERROR_QUIET)
        if(_unsafe EQUAL 0)
            message(FATAL_ERROR "Inspection bypassed generated Metal safety policy")
        endif()
    endif()
    file(SHA256 "${_package}" _good)
    execute_process(COMMAND "${TRUFFLE_SHADERC}" --compile --name portable --target "${_target}"
        --stage "${_stage}" --source-language "${_language}" --input "${_source}" --output "${_package}"
        --allow-experimental-metal --glsl-version 999 RESULT_VARIABLE _bad OUTPUT_QUIET ERROR_QUIET)
    file(SHA256 "${_package}" _retained)
    if(_bad EQUAL 0 OR NOT _good STREQUAL _retained)
        message(FATAL_ERROR "Failed native rebuild changed the previous package")
    endif()
    file(WRITE "${_source}" "#version 450\n#error portable rebuild failure\n")
    execute_process(COMMAND "${TRUFFLE_SHADERC}" --compile --name portable --target "${_target}"
        --stage "${_stage}" --source-language "${_language}" --input "${_source}" --output "${_package}"
        --allow-experimental-metal RESULT_VARIABLE _bad OUTPUT_QUIET ERROR_QUIET)
    file(SHA256 "${_package}" _retained)
    if(_bad EQUAL 0 OR NOT _good STREQUAL _retained)
        message(FATAL_ERROR "Invalid source replaced the previous native package")
    endif()
    math(EXPR _count "${_count}+1")
endforeach()
if(NOT _count EQUAL 42)
    message(FATAL_ERROR "Expected 42 native route/configuration equivalence checks, got ${_count}")
endif()

execute_process(COMMAND "${TRUFFLE_SHADER_EQUIVALENCE}" "${_source}"
    glsl fragment glsl 1.1 1.3 "${_package}" write COMMAND_ERROR_IS_FATAL ANY)
set(_append)
foreach(_target spirv msl glsl glsl-es)
    execute_process(COMMAND "${TRUFFLE_SHADERC}" --compile --name portable
        --target "${_target}" --stage fragment --source-language glsl
        --input "${_source}" --output "${_package}" --allow-experimental-metal ${_append}
        COMMAND_ERROR_IS_FATAL ANY)
    set(_append --append "${_package}")
endforeach()
execute_process(COMMAND "${TRUFFLE_SHADERC}" --compile --name portable
    --target glsl --stage fragment --source-language glsl --define SECOND=1
    --input "${_source}" --output "${_package}" --allow-experimental-metal ${_append}
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package}" --allow-experimental-metal
    OUTPUT_VARIABLE _inspection COMMAND_ERROR_IS_FATAL ANY)
if(NOT _inspection MATCHES "variants=5" OR NOT _inspection MATCHES "remap=1:6")
    message(FATAL_ERROR "Multi-target/configuration assembly lost translator metadata: ${_inspection}")
endif()
