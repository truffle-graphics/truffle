cmake_minimum_required(VERSION 3.23)

project(
    Truffle
    VERSION 0.1.0
    DESCRIPTION "Embeddable graphics engine and rendering system"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(TruffleOptions)
include(TruffleFormatting)
include(TruffleInstall)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

add_subdirectory(src)

if(TRUFFLE_BUILD_SHADERC)
    add_subdirectory(tools/shaderc)
endif()

if(TRUFFLE_BUILD_RHI_DOCTOR)
    add_subdirectory(tools/rhi-doctor)
endif()

if(TRUFFLE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TRUFFLE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

truffle_add_format_targets()

if(TRUFFLE_INSTALL)
    truffle_install_package()
    include(TruffleCPack)
endif()
