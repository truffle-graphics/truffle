cmake_minimum_required(VERSION 3.23)

project(TrufflePackageConsumer LANGUAGES CXX)

find_package(Truffle CONFIG REQUIRED)

add_executable(truffle_package_consumer main.cpp)
target_link_libraries(
    truffle_package_consumer
    PRIVATE
        Truffle::Core
        Truffle::Assets
        Truffle::ECS
        Truffle::RHI
        Truffle::BackendNull
        Truffle::Render
        Truffle::Scene
        Truffle::Diagnostics)
