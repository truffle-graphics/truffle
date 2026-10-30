cmake_minimum_required(VERSION 3.23)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TruffleShaderWorkflow LANGUAGES CXX)
    find_package(Truffle CONFIG REQUIRED)
endif()

if(NOT TARGET Truffle::Shader)
    return()
endif()

add_executable(truffle_shader_workflow main.cpp)
target_link_libraries(truffle_shader_workflow PRIVATE Truffle::Shader)
if(COMMAND truffle_target_warnings)
    truffle_target_warnings(truffle_shader_workflow)
endif()

if(TRUFFLE_BUILD_TESTS AND TRUFFLE_SHADER_ENABLE_GLSLANG)
    add_test(NAME truffle_shader_workflow
        COMMAND truffle_shader_workflow "${CMAKE_CURRENT_SOURCE_DIR}/shader.frag")
endif()
