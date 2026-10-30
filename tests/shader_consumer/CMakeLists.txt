cmake_minimum_required(VERSION 3.23)
project(TruffleShaderConsumer LANGUAGES CXX)
find_package(Truffle CONFIG REQUIRED)
add_executable(shader_consumer main.cpp)
target_link_libraries(shader_consumer PRIVATE Truffle::Shader)
target_compile_definitions(shader_consumer PRIVATE EXPECT_COMPILER=${EXPECT_COMPILER})

# Renderer links Shader directly; its link line is compiler-free whenever the
# install was built without compiler adapters.
add_executable(prepared_consumer prepared.cpp)
target_link_libraries(prepared_consumer PRIVATE Truffle::Renderer Truffle::RHI)
target_compile_definitions(prepared_consumer PRIVATE EXPECT_COMPILER=${EXPECT_COMPILER})
set_target_properties(shader_consumer prepared_consumer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<0:>")
