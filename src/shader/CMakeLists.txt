add_library(truffle_shader STATIC compiler.cpp)
target_link_libraries(truffle_shader PUBLIC truffle_rhi)
truffle_configure_target(truffle_shader Shader)
add_library(Truffle::Shader ALIAS truffle_shader)

if(TRUFFLE_SHADER_ENABLE_GLSLANG)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/glslang/CMakeLists.txt")
        message(FATAL_ERROR "Initialize the pinned vendor/glslang submodule")
    endif()
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
