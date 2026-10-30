if(NOT DEFINED TRUFFLE_SHADERC OR NOT DEFINED TRUFFLE_SHADER_EQUIVALENCE OR
   NOT DEFINED TRUFFLE_SHADERC_TEST_DIR)
    message(FATAL_ERROR "Configured shaderc smoke inputs missing")
endif()

file(MAKE_DIRECTORY "${TRUFFLE_SHADERC_TEST_DIR}/include")
set(_root "${TRUFFLE_SHADERC_TEST_DIR}/configured.comp")
set(_include "${TRUFFLE_SHADERC_TEST_DIR}/include")
set(_package "${TRUFFLE_SHADERC_TEST_DIR}/configured.tsp")
file(WRITE "${_root}" "#version 450\n#include \"module.glsl\"\n"
    "layout(local_size_x=2) in;\n"
    "layout(constant_id=7) const uint increment=9;\n"
    "layout(push_constant) uniform Push { layout(offset=16) uint base; } pc;\n"
    "layout(set=0,binding=0) buffer Output { uint values[]; } data[2];\n"
    "void main(){ data[1].values[gl_GlobalInvocationID.x]=value()+increment+pc.base; }\n")
file(WRITE "${TRUFFLE_SHADERC_TEST_DIR}/module.glsl"
    "#include <configuration.glsl>\nuint value(){ return uint(QUALITY)*FACTOR; }\n")
file(WRITE "${_include}/configuration.glsl"
    "#ifndef FLAG\n#error FLAG is required\n#endif\n#define FACTOR 3u\n")
set(_args --compile --name configured --target spirv --stage compute
    --input "${_root}" --output "${_package}" --source-language glsl
    --include-root "${_include}")
execute_process(COMMAND "${TRUFFLE_SHADERC}" ${_args} --define QUALITY=2 --define FLAG
    RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Configured CLI compilation failed: ${_error}")
endif()
execute_process(COMMAND "${TRUFFLE_SHADER_EQUIVALENCE}" "${_root}" "${_include}" "${_package}"
    COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${_package}" _initial_hash)
execute_process(COMMAND "${TRUFFLE_SHADERC}" ${_args} --define FLAG --define QUALITY=2
    COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${_package}" _repeat_hash)
if(NOT _initial_hash STREQUAL _repeat_hash)
    message(FATAL_ERROR "Define ordering changed the package")
endif()
execute_process(COMMAND "${TRUFFLE_SHADERC}" ${_args}
    --define FLAG --define QUALITY=3 --append "${_package}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _inspection)
if(NOT _result EQUAL 0 OR NOT _inspection MATCHES "variants=2" OR
   NOT _inspection MATCHES "sources=3" OR NOT _inspection MATCHES "configurations=2" OR
   NOT _inspection MATCHES "bindings=1" OR
   NOT _inspection MATCHES "count=2 minimum-size=4 readonly=0" OR
   NOT _inspection MATCHES "push-offset=16 size=4" OR
   NOT _inspection MATCHES "specialization-id=7 name=increment type=2 default-bits=9")
    message(FATAL_ERROR "Configured append/inspection failed: ${_inspection}")
endif()

function(expect_failure expected)
    execute_process(COMMAND "${TRUFFLE_SHADERC}" ${_args} ${ARGN}
        RESULT_VARIABLE _result ERROR_VARIABLE _error OUTPUT_VARIABLE _output)
    if(_result EQUAL 0 OR NOT _error MATCHES "${expected}")
        message(FATAL_ERROR "Expected '${expected}', got ${_result}: ${_output}${_error}")
    endif()
endfunction()
expect_failure("FLAG is required" --define QUALITY=2)
expect_failure("Invalid/duplicate" --define FLAG --define QUALITY=2 --define QUALITY=3)
expect_failure("Invalid/duplicate" --define FLAG --define BAD-NAME=2)
expect_failure("source-hash" --define FLAG --define QUALITY=2 --source-hash
    0000000000000000000000000000000000000000000000000000000000000000)
file(WRITE "${_include}/configuration.glsl" "#include \"absent.glsl\"\n")
expect_failure("Missing shader include" --define FLAG --define QUALITY=2)
file(WRITE "${_include}/configuration.glsl" "#include \"configuration.glsl\"\n")
expect_failure("Cyclic shader include" --define FLAG --define QUALITY=2)

# Failed CLI rebuilds must return failure without replacing a last-good artifact.
execute_process(COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _inspection)
if(NOT _result EQUAL 0 OR NOT _inspection MATCHES "variants=2")
    message(FATAL_ERROR "Failed compilation damaged the separately retained last-good artifact")
endif()
