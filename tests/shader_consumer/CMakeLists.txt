cmake_minimum_required(VERSION 3.23)
project(TruffleShaderConsumer LANGUAGES CXX)
find_package(Truffle CONFIG REQUIRED)
add_executable(shader_consumer main.cpp)
target_link_libraries(shader_consumer PRIVATE Truffle::Shader)
target_compile_definitions(shader_consumer PRIVATE EXPECT_COMPILER=${EXPECT_COMPILER})

# This target must retain a compiler-free link line even in a compiler-enabled install.
add_executable(prepared_consumer prepared.cpp)
target_link_libraries(prepared_consumer PRIVATE Truffle::Renderer Truffle::RHI)
