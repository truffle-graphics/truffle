file(MAKE_DIRECTORY "${TRUFFLE_SHADERC_TEST_DIR}")
set(source "${TRUFFLE_SHADERC_TEST_DIR}/fixture.wgsl")
set(package "${TRUFFLE_SHADERC_TEST_DIR}/fixture.tsp")
file(WRITE "${source}" "struct Output { values: array<u32, 4> }\n"
    "@group(1) @binding(9) var<storage, read_write> outputData: Output;\n"
    "fn value(i: u32) -> u32 { return 12u + i; }\n"
    "@compute @workgroup_size(2) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
    "outputData.values[id.x] = value(id.x);\n}\n")
set(args --compile --name wgsl --stage compute --source-language wgsl --input "${source}"
    --output "${package}" --allow-experimental-metal --allow-experimental-wgsl)
foreach(target spirv wgsl)
    execute_process(COMMAND "${TRUFFLE_SHADERC}" ${args} --target "${target}"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${TRUFFLE_SHADER_EQUIVALENCE}" "${source}" "${target}" "${package}"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${TRUFFLE_SHADERC}" --inspect "${package}" --allow-experimental-wgsl
        OUTPUT_VARIABLE inspection COMMAND_ERROR_IS_FATAL ANY)
    if(NOT inspection MATCHES "minimum-size=16 readonly=0" OR
       NOT inspection MATCHES "compiler=Tint")
        message(FATAL_ERROR "WGSL canonical reflection/provenance missing: ${inspection}")
    endif()
    if(target STREQUAL "spirv")
        set(intermediate "${TRUFFLE_SHADERC_TEST_DIR}/fixture.spv")
        execute_process(COMMAND "${TRUFFLE_SHADER_EQUIVALENCE}" --extract "${package}" "${intermediate}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${TRUFFLE_SHADERC}" ${args} --target wgsl
            --source-language spirv --input "${intermediate}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${TRUFFLE_SHADER_EQUIVALENCE}" "${intermediate}" wgsl "${package}" spirv
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()
file(SHA256 "${package}" good)
function(reject expected)
    execute_process(COMMAND "${TRUFFLE_SHADERC}" ${args} --target spirv ${ARGN}
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT error MATCHES "${expected}")
        message(FATAL_ERROR "Expected ${expected}, got ${result}: ${error}")
    endif()
    file(SHA256 "${package}" retained)
    if(NOT good STREQUAL retained)
        message(FATAL_ERROR "Failed WGSL compilation replaced last-good file")
    endif()
endfunction()
reject("no preprocessor" --define FLAG)
reject("no preprocessor" --include-root "${TRUFFLE_SHADERC_TEST_DIR}")
reject("entry point not found" --entry absent)
reject("stage does not match" --stage vertex)
file(WRITE "${source}" "@compute fn main( invalid )")
reject("WGSL validation failed")
file(WRITE "${source}" "@compute @workgroup_size(1) fn main() {}")
execute_process(COMMAND "${TRUFFLE_SHADERC}" ${args} --target spirv COMMAND_ERROR_IS_FATAL ANY)
