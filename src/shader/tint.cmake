truffle_shader_require_pin(dawn 46b4670bc67cb4f6d34f6ce6a46ba7e1d6059abf)
truffle_shader_require_pin(abseil 04dc59d2c83238cb1fcb49083e5e416643a899ce)

foreach(flag D3D11 D3D12 METAL NULL DESKTOP_GL OPENGLES VULKAN WEBGPU_ON_WEBGPU
             SPIRV_VALIDATION INSTALL SWIFTSHADER)
    set(DAWN_ENABLE_${flag} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(flag SAMPLES TESTS NODE_BINDINGS BENCHMARKS PROTOBUF MONOLITHIC_LIBRARY)
    set(DAWN_BUILD_${flag} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(flag WAYLAND X11 GLFW WINDOWS_UI BUILT_DXC)
    set(DAWN_USE_${flag} OFF CACHE BOOL "" FORCE)
endforeach()
set(DAWN_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(DAWN_ABSEIL_DIR "${PROJECT_SOURCE_DIR}/vendor/abseil")
set(DAWN_SPIRV_HEADERS_DIR "${PROJECT_SOURCE_DIR}/vendor/spirv-headers")
set(DAWN_SPIRV_TOOLS_DIR "${PROJECT_SOURCE_DIR}/vendor/spirv-tools")
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
foreach(flag CMD_TOOLS TESTS FUZZERS BENCHMARKS TINTD IR_BINARY GLSL_WRITER
             GLSL_VALIDATOR HLSL_WRITER MSL_WRITER SYNTAX_TREE_WRITER)
    set(TINT_BUILD_${flag} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(flag WGSL_READER WGSL_WRITER SPV_WRITER SPV_READER)
    set(TINT_BUILD_${flag} ON CACHE BOOL "" FORCE)
endforeach()
set(TINT_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
# Tint's SPIR-V reader uses optimizer internal headers and generated tables.
set(spirv-tools_BINARY_DIR "${PROJECT_BINARY_DIR}/dependencies/spirv-tools")
add_subdirectory("${PROJECT_SOURCE_DIR}/vendor/dawn"
                 "${PROJECT_BINARY_DIR}/dependencies/dawn" EXCLUDE_FROM_ALL)
target_sources(truffle_shader PRIVATE tint_adapter.cpp)
target_link_libraries(truffle_shader PRIVATE tint_lang_wgsl_reader
    tint_lang_wgsl_inspector tint_lang_wgsl_writer tint_lang_spirv_writer
    tint_lang_spirv_writer_helpers tint_lang_spirv_reader)
target_compile_definitions(truffle_shader PRIVATE TRUFFLE_SHADER_HAS_TINT=1
    TRUFFLE_TINT_REVISION="46b4670bc67cb4f6d34f6ce6a46ba7e1d6059abf"
    TRUFFLE_ABSEIL_REVISION="04dc59d2c83238cb1fcb49083e5e416643a899ce")

# Tint exposes many granular static archives, but no upstream CMake export.
# Export only the transitive closure used here; headers stay build-private.
function(truffle_install_tint_closure target)
    if(NOT TARGET "${target}")
        return()
    endif()
    get_target_property(alias "${target}" ALIASED_TARGET)
    if(alias)
        set(target "${alias}")
    endif()
    get_property(visited GLOBAL PROPERTY TRUFFLE_TINT_EXPORTS)
    if(target IN_LIST visited)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY TRUFFLE_TINT_EXPORTS "${target}")
    if(target MATCHES "^tint_" OR target MATCHES "^dawn_" OR target MATCHES "^absl_")
        get_target_property(includes "${target}" INTERFACE_INCLUDE_DIRECTORIES)
        if(includes)
            set_property(TARGET "${target}" PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                         "$<BUILD_INTERFACE:${includes}>")
        endif()
        get_target_property(sources "${target}" INTERFACE_SOURCES)
        if(sources)
            set_property(TARGET "${target}" PROPERTY INTERFACE_SOURCES
                         "$<BUILD_INTERFACE:${sources}>")
        endif()
        install(TARGETS "${target}" EXPORT TruffleTargets
                ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
        get_target_property(deps "${target}" LINK_LIBRARIES)
        get_target_property(interface_deps "${target}" INTERFACE_LINK_LIBRARIES)
        list(APPEND deps ${interface_deps})
        foreach(dep IN LISTS deps)
            string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" dep "${dep}")
            truffle_install_tint_closure("${dep}")
        endforeach()
    elseif(target STREQUAL "SPIRV-Headers")
        install(TARGETS "${target}" EXPORT TruffleTargets)
    elseif(target STREQUAL "SPIRV-Tools-opt")
        install(TARGETS "${target}" EXPORT TruffleTargets
                ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
endfunction()
if(TRUFFLE_INSTALL)
    foreach(root tint_lang_wgsl_reader tint_lang_wgsl_inspector tint_lang_wgsl_writer
                 tint_lang_spirv_writer tint_lang_spirv_writer_helpers tint_lang_spirv_reader)
        truffle_install_tint_closure("${root}")
    endforeach()
    install(FILES "${PROJECT_SOURCE_DIR}/vendor/dawn/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/tint")
    install(FILES "${PROJECT_SOURCE_DIR}/vendor/abseil/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/abseil")
endif()
