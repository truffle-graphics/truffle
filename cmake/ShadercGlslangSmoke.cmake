if(NOT DEFINED TRUFFLE_SHADERC OR NOT DEFINED TRUFFLE_SHADERC_TEST_DIR)
    message(FATAL_ERROR "shaderc glslang smoke inputs are missing")
endif()

file(MAKE_DIRECTORY "${TRUFFLE_SHADERC_TEST_DIR}")
set(_vertex "${TRUFFLE_SHADERC_TEST_DIR}/triangle.vert")
set(_package_a "${TRUFFLE_SHADERC_TEST_DIR}/triangle-a.truffle-shader")
set(_package_b "${TRUFFLE_SHADERC_TEST_DIR}/triangle-b.truffle-shader")
file(WRITE "${_vertex}"
    "#version 450\n"
    "layout(location = 0) out vec3 color;\n"
    "void main() {\n"
    "  const vec2 positions[3] = vec2[3](vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));\n"
    "  const vec3 colors[3] = vec3[3](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));\n"
    "  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);\n"
    "  color = colors[gl_VertexIndex];\n"
    "}\n")
file(SHA256 "${_vertex}" _vertex_hash)

set(_compile_arguments
    --compile
    --name triangle
    --target spirv
    --stage vertex
    --entry main
    --input "${_vertex}"
    --source-hash "${_vertex_hash}"
    --source-language glsl)

execute_process(
    COMMAND "${TRUFFLE_SHADERC}" ${_compile_arguments} --output "${_package_a}"
    RESULT_VARIABLE _compile_a_result
    OUTPUT_VARIABLE _compile_a_output
    ERROR_VARIABLE _compile_a_error)
if(NOT _compile_a_result EQUAL 0)
    message(FATAL_ERROR
        "GLSL to SPIR-V compilation failed: ${_compile_a_output}${_compile_a_error}")
endif()

execute_process(
    COMMAND "${TRUFFLE_SHADERC}" ${_compile_arguments} --output "${_package_b}"
    RESULT_VARIABLE _compile_b_result
    OUTPUT_VARIABLE _compile_b_output
    ERROR_VARIABLE _compile_b_error)
if(NOT _compile_b_result EQUAL 0)
    message(FATAL_ERROR
        "repeat GLSL compilation failed: ${_compile_b_output}${_compile_b_error}")
endif()

file(SHA256 "${_package_a}" _package_a_hash)
file(SHA256 "${_package_b}" _package_b_hash)
if(NOT _package_a_hash STREQUAL _package_b_hash)
    message(FATAL_ERROR "GLSL compilation produced non-deterministic packages")
endif()

set(_fragment "${TRUFFLE_SHADERC_TEST_DIR}/triangle.frag")
set(_fragment_package
    "${TRUFFLE_SHADERC_TEST_DIR}/triangle-fragment.truffle-shader")
file(WRITE "${_fragment}"
    "#version 450\n"
    "layout(location = 0) out vec4 outputColor;\n"
    "void main() { outputColor = vec4(1.0, 0.0, 0.0, 1.0); }\n")
file(SHA256 "${_fragment}" _fragment_hash)
execute_process(
    COMMAND "${TRUFFLE_SHADERC}"
        --compile
        --name triangle-fragment
        --target spirv
        --stage fragment
        --entry main
        --input "${_fragment}"
        --output "${_fragment_package}"
        --source-hash "${_fragment_hash}"
        --source-language glsl
    RESULT_VARIABLE _fragment_result
    OUTPUT_VARIABLE _fragment_output
    ERROR_VARIABLE _fragment_error)
if(NOT _fragment_result EQUAL 0)
    message(FATAL_ERROR
        "fragment GLSL to SPIR-V compilation failed: ${_fragment_output}${_fragment_error}")
endif()

set(_mrt_fragment "${TRUFFLE_SHADERC_TEST_DIR}/mrt.frag")
set(_mrt_fragment_package
    "${TRUFFLE_SHADERC_TEST_DIR}/mrt-fragment.truffle-shader")
file(WRITE "${_mrt_fragment}"
    "#version 450\n"
    "layout(location = 0) out vec4 firstColor;\n"
    "layout(location = 1) out vec4 secondColor;\n"
    "void main() {\n"
    "  firstColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "  secondColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
    "}\n")
file(SHA256 "${_mrt_fragment}" _mrt_fragment_hash)
execute_process(
    COMMAND "${TRUFFLE_SHADERC}"
        --compile
        --name mrt-fragment
        --target spirv
        --stage fragment
        --entry main
        --input "${_mrt_fragment}"
        --output "${_mrt_fragment_package}"
        --source-hash "${_mrt_fragment_hash}"
        --source-language glsl
    RESULT_VARIABLE _mrt_fragment_result
    OUTPUT_VARIABLE _mrt_fragment_output
    ERROR_VARIABLE _mrt_fragment_error)
if(NOT _mrt_fragment_result EQUAL 0)
    message(FATAL_ERROR
        "MRT fragment compilation failed: ${_mrt_fragment_output}${_mrt_fragment_error}")
endif()

set(_textured_fragment "${TRUFFLE_SHADERC_TEST_DIR}/textured.frag")
set(_textured_fragment_package
    "${TRUFFLE_SHADERC_TEST_DIR}/textured-fragment.truffle-shader")
file(WRITE "${_textured_fragment}"
    "#version 450\n"
    "layout(set = 0, binding = 0) uniform texture2D sourceTexture;\n"
    "layout(set = 0, binding = 1) uniform sampler sourceSampler;\n"
    "layout(location = 0) out vec4 outputColor;\n"
    "void main() { outputColor = texture(sampler2D(sourceTexture, sourceSampler), vec2(0.5)); }\n")
file(SHA256 "${_textured_fragment}" _textured_fragment_hash)
execute_process(
    COMMAND "${TRUFFLE_SHADERC}"
        --compile
        --name textured-fragment
        --target spirv
        --stage fragment
        --entry main
        --input "${_textured_fragment}"
        --output "${_textured_fragment_package}"
        --source-hash "${_textured_fragment_hash}"
        --source-language glsl
    RESULT_VARIABLE _textured_fragment_result
    OUTPUT_VARIABLE _textured_fragment_output
    ERROR_VARIABLE _textured_fragment_error)
if(NOT _textured_fragment_result EQUAL 0)
    message(FATAL_ERROR
        "textured fragment compilation failed: ${_textured_fragment_output}${_textured_fragment_error}")
endif()

execute_process(
    COMMAND "${TRUFFLE_SHADERC}" --inspect "${_package_a}"
    RESULT_VARIABLE _inspect_result
    OUTPUT_VARIABLE _inspect_output
    ERROR_VARIABLE _inspect_error)
if(NOT _inspect_result EQUAL 0 OR
   NOT _inspect_output MATCHES "triangle variants=1 compilers=1" OR
   NOT _inspect_output MATCHES "compiler=glslang version=16\\.5\\.0 revision=a8d28bd082bff18ffbe80996e922b012f915cf07")
    message(FATAL_ERROR
        "compiled package inspection failed: ${_inspect_output}${_inspect_error}")
endif()

set(_compute_es "${TRUFFLE_SHADERC_TEST_DIR}/work.comp")
set(_compute_es_package "${TRUFFLE_SHADERC_TEST_DIR}/work-es.truffle-shader")
file(WRITE "${_compute_es}"
    "#version 310 es\n"
    "layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;\n"
    "layout(set = 0, binding = 0, std430) buffer Output { uint values[]; } outputData;\n"
    "void main() {\n"
    "  uint index = gl_GlobalInvocationID.x;\n"
    "  outputData.values[index] = 0x10203040u + index * 0x01010101u;\n"
    "}\n")
file(SHA256 "${_compute_es}" _compute_es_hash)
execute_process(
    COMMAND "${TRUFFLE_SHADERC}"
        --compile
        --name work-es
        --target spirv
        --stage compute
        --input "${_compute_es}"
        --output "${_compute_es_package}"
        --source-hash "${_compute_es_hash}"
        --source-language glsl-es
    RESULT_VARIABLE _compile_es_result
    OUTPUT_VARIABLE _compile_es_output
    ERROR_VARIABLE _compile_es_error)
if(NOT _compile_es_result EQUAL 0)
    message(FATAL_ERROR
        "GLSL ES to SPIR-V compilation failed: ${_compile_es_output}${_compile_es_error}")
endif()

set(_invalid "${TRUFFLE_SHADERC_TEST_DIR}/invalid.vert")
file(WRITE "${_invalid}" "#version 450\nvoid main( {\n")
file(SHA256 "${_invalid}" _invalid_hash)
execute_process(
    COMMAND "${TRUFFLE_SHADERC}"
        --compile
        --name invalid
        --target spirv
        --stage vertex
        --input "${_invalid}"
        --output "${TRUFFLE_SHADERC_TEST_DIR}/invalid.truffle-shader"
        --source-hash "${_invalid_hash}"
        --source-language glsl
    RESULT_VARIABLE _invalid_result
    OUTPUT_VARIABLE _invalid_output
    ERROR_VARIABLE _invalid_error)
if(_invalid_result EQUAL 0 OR NOT _invalid_error MATCHES "glslang")
    message(FATAL_ERROR
        "invalid GLSL did not return compiler diagnostics: ${_invalid_output}${_invalid_error}")
endif()
