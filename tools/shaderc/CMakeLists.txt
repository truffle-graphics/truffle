add_executable(truffle-shaderc main.cpp)
target_link_libraries(truffle-shaderc PRIVATE truffle_rhi)
target_compile_features(truffle-shaderc PRIVATE cxx_std_20)
truffle_target_warnings(truffle-shaderc)

if(TRUFFLE_SHADERC_ENABLE_GLSLANG)
    set(_truffle_glslang_dir "${PROJECT_SOURCE_DIR}/vendor/glslang")
    if(NOT EXISTS "${_truffle_glslang_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "TRUFFLE_SHADERC_ENABLE_GLSLANG requires vendor/glslang. "
            "Run: git submodule update --init vendor/glslang")
    endif()

    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${_truffle_glslang_dir}"
                     "${PROJECT_BINARY_DIR}/dependencies/glslang"
                     EXCLUDE_FROM_ALL)
    # glslang assigns a Windows-only debug postfix inside its subdirectory even
    # when the parent single-config build has no configuration. Keep producer
    # and consumer artifact names aligned for MinGW and other Windows generators.
    set_target_properties(glslang SPIRV glslang-default-resource-limits
        PROPERTIES DEBUG_POSTFIX "")

    target_sources(truffle-shaderc PRIVATE glslang_compiler.cpp)
    target_link_libraries(truffle-shaderc PRIVATE
        glslang::glslang-default-resource-limits
        glslang::SPIRV)
    target_compile_definitions(truffle-shaderc PRIVATE
        TRUFFLE_SHADERC_HAS_GLSLANG=1
        TRUFFLE_GLSLANG_REVISION="a8d28bd082bff18ffbe80996e922b012f915cf07")
endif()

if(TRUFFLE_INSTALL)
    install(TARGETS truffle-shaderc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
