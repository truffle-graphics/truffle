include(ExternalProject)
find_package(Git REQUIRED QUIET)

truffle_shader_require_pin(dxc 01b62ad47db7dee3dd33f90e7339cf9e860b3f93)
foreach(dependency SPIRV-Headers SPIRV-Tools DirectX-Headers)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/dxc/external/${dependency}/CMakeLists.txt")
        message(FATAL_ERROR
            "Initialize DXC dependencies: git -C vendor/dxc submodule update --init external/${dependency}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/vendor/dxc"
                submodule status "external/${dependency}"
        RESULT_VARIABLE status_result OUTPUT_VARIABLE status)
    if(NOT status_result EQUAL 0 OR NOT status MATCHES "^ ")
        message(FATAL_ERROR "DXC requires its pinned external/${dependency} submodule")
    endif()
endforeach()

set(_truffle_dxc_binary "${PROJECT_BINARY_DIR}/dependencies/dxc")
if(WIN32)
    set(_truffle_dxc_library "${_truffle_dxc_binary}/bin/dxcompiler.dll")
    set(_truffle_dxc_implib "${_truffle_dxc_binary}/lib/dxcompiler.lib")
elseif(APPLE)
    set(_truffle_dxc_library "${_truffle_dxc_binary}/lib/libdxcompiler.dylib")
else()
    set(_truffle_dxc_library "${_truffle_dxc_binary}/lib/libdxcompiler.so")
endif()

# Isolate DXC's LLVM build settings and targets from the consuming project.
ExternalProject_Add(truffle_dxc_build
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/vendor/dxc"
    BINARY_DIR "${_truffle_dxc_binary}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        -C "${PROJECT_SOURCE_DIR}/vendor/dxc/cmake/caches/PredefinedParams.cmake"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
        "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        -DCMAKE_BUILD_TYPE=Release
        -DHLSL_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_TESTS=OFF
        -DSPIRV_BUILD_TESTS=OFF
        -DHLSL_BUILD_DXILCONV=OFF
        -DENABLE_SPIRV_CODEGEN=ON
        -DLLVM_ENABLE_ZLIB=OFF
        -DLLVM_ENABLE_TERMINFO=OFF
        -DLLVM_ENABLE_ASSERTIONS=OFF
        -DLLVM_ENABLE_WERROR=OFF
        -DLLVM_PARALLEL_COMPILE_JOBS=2
        -DLLVM_PARALLEL_LINK_JOBS=1
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target dxcompiler --parallel 2
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${_truffle_dxc_library}" ${_truffle_dxc_implib})

add_library(Truffle::DXC SHARED IMPORTED GLOBAL)
set_target_properties(Truffle::DXC PROPERTIES
    IMPORTED_LOCATION "${_truffle_dxc_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/vendor/dxc/include")
if(WIN32)
    set_property(TARGET Truffle::DXC PROPERTY IMPORTED_IMPLIB "${_truffle_dxc_implib}")
endif()
add_dependencies(Truffle::DXC truffle_dxc_build)

if(TRUFFLE_INSTALL)
    install(DIRECTORY "${_truffle_dxc_binary}/lib/"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            FILES_MATCHING PATTERN "*dxcompiler*")
    if(WIN32)
        install(FILES "${_truffle_dxc_library}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
    install(FILES "${PROJECT_SOURCE_DIR}/vendor/dxc/LICENSE.TXT"
                  "${PROJECT_SOURCE_DIR}/vendor/dxc/ThirdPartyNotices.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/dxc")
    foreach(dependency SPIRV-Headers SPIRV-Tools DirectX-Headers)
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/dxc/external/${dependency}/LICENSE"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/dxc/${dependency}")
    endforeach()
endif()
