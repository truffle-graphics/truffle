add_library(truffle_shader STATIC compiler.cpp)
target_link_libraries(truffle_shader PUBLIC truffle_rhi)
truffle_configure_target(truffle_shader Shader)
add_library(Truffle::Shader ALIAS truffle_shader)

if(TRUFFLE_SHADER_ENABLE_METALLIB_COMPILER AND
   NOT TRUFFLE_SHADER_ENABLE_METAL_COMPILER)
    message(FATAL_ERROR "TRUFFLE_SHADER_ENABLE_METALLIB_COMPILER requires TRUFFLE_SHADER_ENABLE_METAL_COMPILER")
endif()

function(truffle_shader_require_pin name revision)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/${name}/CMakeLists.txt")
        message(FATAL_ERROR "Initialize pinned sources: git submodule update --init vendor/${name}")
    endif()
    find_package(Git REQUIRED QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/vendor/${name}" rev-parse HEAD
                    RESULT_VARIABLE result OUTPUT_VARIABLE actual OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/vendor/${name}"
                    status --porcelain --untracked-files=no
                    RESULT_VARIABLE status_result OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0 OR NOT status_result EQUAL 0 OR NOT actual STREQUAL revision OR changes)
        message(FATAL_ERROR "Shader requires clean vendor/${name} at ${revision}; initialize the pinned submodule")
    endif()
endfunction()

if(TRUFFLE_SHADER_ENABLE_METAL_COMPILER)
    find_library(TRUFFLE_METAL_FRAMEWORK Metal REQUIRED)
    find_library(TRUFFLE_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_sources(truffle_shader PRIVATE metal_adapter.mm)
    target_link_libraries(truffle_shader PRIVATE
        "${TRUFFLE_METAL_FRAMEWORK}" "${TRUFFLE_FOUNDATION_FRAMEWORK}")
    target_compile_definitions(truffle_shader PRIVATE
        TRUFFLE_SHADER_HAS_METAL_COMPILER=1)
endif()

if(TRUFFLE_SHADER_ENABLE_METALLIB_COMPILER)
    find_program(TRUFFLE_XCRUN_EXECUTABLE xcrun REQUIRED)
    execute_process(COMMAND "${TRUFFLE_XCRUN_EXECUTABLE}" --find metal
        RESULT_VARIABLE metal_result OUTPUT_VARIABLE metal_path
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${TRUFFLE_XCRUN_EXECUTABLE}" --find metallib
        RESULT_VARIABLE metallib_result OUTPUT_VARIABLE metallib_path
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT metal_result EQUAL 0 OR NOT metallib_result EQUAL 0 OR
       NOT EXISTS "${metal_path}" OR NOT EXISTS "${metallib_path}")
        message(FATAL_ERROR
            "TRUFFLE_SHADER_ENABLE_METALLIB_COMPILER requires the installed Apple Metal Toolchain (xcrun metal and metallib)")
    endif()
    target_compile_definitions(truffle_shader PRIVATE
        TRUFFLE_SHADER_HAS_METALLIB_COMPILER=1
        TRUFFLE_XCRUN_EXECUTABLE="${TRUFFLE_XCRUN_EXECUTABLE}")
endif()

if(TRUFFLE_SHADER_ENABLE_SPIRV_CROSS AND
   (NOT (TRUFFLE_SHADER_ENABLE_GLSLANG OR TRUFFLE_SHADER_ENABLE_TINT OR TRUFFLE_SHADER_ENABLE_DXC) OR NOT TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS))
    message(FATAL_ERROR "SPIRV-Cross routes require a SPIR-V source adapter and TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS")
endif()
if(TRUFFLE_SHADER_ENABLE_DXC AND TRUFFLE_SHADER_ENABLE_SPIRV_CROSS AND
   NOT TRUFFLE_SHADER_ENABLE_GLSLANG)
    message(FATAL_ERROR "Portable HLSL routes require TRUFFLE_SHADER_ENABLE_GLSLANG to validate generated GLSL")
endif()
if(TRUFFLE_SHADER_ENABLE_TINT AND NOT TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS)
    message(FATAL_ERROR "Tint requires TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS for canonical SPIR-V validation")
endif()
if(TRUFFLE_SHADER_ENABLE_GLSLANG OR TRUFFLE_SHADER_ENABLE_TINT OR TRUFFLE_SHADER_ENABLE_DXC)
    truffle_shader_require_pin(spirv-headers 29981f65241605e08b0ede4cfeb999fe3b723c6a)
    target_sources(truffle_shader PRIVATE spirv_reflection.cpp)
    target_include_directories(truffle_shader SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/vendor/spirv-headers/include")
endif()
if(TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS)
    truffle_shader_require_pin(spirv-tools b707790a898e44038547df54580022fc1cf89c3d)
    truffle_shader_require_pin(spirv-headers 29981f65241605e08b0ede4cfeb999fe3b723c6a)
    set(SPIRV-Headers_SOURCE_DIR "${PROJECT_SOURCE_DIR}/vendor/spirv-headers")
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_USE_MIMALLOC_IN_STATIC_BUILD OFF CACHE BOOL "" FORCE)
    # Export only the validation library used by Shader, not optimizer/linker tools.
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/vendor/spirv-tools"
                     "${PROJECT_BINARY_DIR}/dependencies/spirv-tools" EXCLUDE_FROM_ALL)
    if(TRUFFLE_INSTALL)
        install(TARGETS SPIRV-Tools-static EXPORT TruffleTargets
                ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/spirv-tools/LICENSE"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/spirv-tools")
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/spirv-headers/LICENSE"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/spirv-headers")
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/vendor/spirv-headers/LICENSES/"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/spirv-headers")
    endif()
    target_sources(truffle_shader PRIVATE spirv_validation.cpp)
    target_link_libraries(truffle_shader PRIVATE SPIRV-Tools-static)
    target_compile_definitions(truffle_shader PRIVATE
        TRUFFLE_SHADER_HAS_SPIRV_TOOLS=1
        TRUFFLE_SPIRV_TOOLS_REVISION="b707790a898e44038547df54580022fc1cf89c3d"
        TRUFFLE_SPIRV_HEADERS_REVISION="29981f65241605e08b0ede4cfeb999fe3b723c6a")
endif()
if(TRUFFLE_SHADER_ENABLE_SPIRV_CROSS)
    truffle_shader_require_pin(spirv-cross 1a6169566c73d3da552748fc372fe2bbb856e46e)
    foreach(option CLI ENABLE_TESTS ENABLE_HLSL ENABLE_CPP ENABLE_REFLECT ENABLE_C_API ENABLE_UTIL)
        set(SPIRV_CROSS_${option} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_NAMESPACE_OVERRIDE truffle_spirv_cross CACHE STRING "" FORCE)
    set(SPIRV_CROSS_SPV_HEADER_NAMESPACE_OVERRIDE truffle_spirv_cross_spv CACHE STRING "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL ON CACHE BOOL "" FORCE)
    if(TRUFFLE_INSTALL)
        set(SPIRV_CROSS_SKIP_INSTALL OFF CACHE BOOL "" FORCE)
    else()
        set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    endif()
    add_subdirectory("${PROJECT_SOURCE_DIR}/vendor/spirv-cross"
                     "${PROJECT_BINARY_DIR}/dependencies/spirv-cross")
    target_sources(truffle_shader PRIVATE spirv_translation.cpp)
    target_link_libraries(truffle_shader PRIVATE spirv-cross-msl spirv-cross-glsl)
    target_compile_definitions(truffle_shader PRIVATE
        TRUFFLE_SHADER_HAS_SPIRV_CROSS=1
        TRUFFLE_SPIRV_CROSS_REVISION="1a6169566c73d3da552748fc372fe2bbb856e46e")
    if(TRUFFLE_INSTALL)
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/spirv-cross/LICENSE"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/spirv-cross")
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/vendor/spirv-cross/LICENSES/"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/spirv-cross")
    endif()
endif()

if(TRUFFLE_SHADER_ENABLE_DXC)
    if(NOT TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS)
        message(FATAL_ERROR "DXC routes require TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS")
    endif()
    include(TruffleDXC)
    target_sources(truffle_shader PRIVATE dxc_adapter.cpp dxil_reflection.cpp)
    target_link_libraries(truffle_shader PRIVATE Truffle::DXC)
    target_include_directories(truffle_shader SYSTEM PRIVATE
        "${PROJECT_SOURCE_DIR}/vendor/spirv-headers/include"
        "${PROJECT_SOURCE_DIR}/vendor/dxc/external/DirectX-Headers/include/directx"
        "${PROJECT_SOURCE_DIR}/vendor/dxc/external/DirectX-Headers/include/wsl/stubs")
    target_compile_definitions(truffle_shader PRIVATE
        TRUFFLE_SHADER_HAS_DXC=1
        TRUFFLE_DXC_REVISION="01b62ad47db7dee3dd33f90e7339cf9e860b3f93")
endif()

if(TRUFFLE_SHADER_ENABLE_GLSLANG)
    truffle_shader_require_pin(glslang a8d28bd082bff18ffbe80996e922b012f915cf07)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL ${TRUFFLE_INSTALL} CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/vendor/glslang"
                     "${PROJECT_BINARY_DIR}/dependencies/glslang")
    set_target_properties(glslang SPIRV glslang-default-resource-limits
                          PROPERTIES DEBUG_POSTFIX "")
    # Upstream installs these legacy static stubs, although SPIRV no longer links them.
    if(TRUFFLE_INSTALL AND NOT BUILD_SHARED_LIBS)
        add_dependencies(truffle_shader OSDependent MachineIndependent GenericCodeGen)
    endif()
    target_sources(truffle_shader PRIVATE glslang_adapter.cpp)
    target_link_libraries(truffle_shader PRIVATE
        glslang::SPIRV glslang::glslang-default-resource-limits)
    target_compile_definitions(truffle_shader PRIVATE
        TRUFFLE_SHADER_HAS_GLSLANG=1
        TRUFFLE_GLSLANG_REVISION="a8d28bd082bff18ffbe80996e922b012f915cf07")
endif()

if(TRUFFLE_SHADER_ENABLE_TINT)
    include("${CMAKE_CURRENT_SOURCE_DIR}/tint.cmake")
endif()
