add_executable(truffle_encom_tests encom_contract_tests.cpp)
add_executable(truffle_shader_compiler_tests shader_compiler_tests.cpp)
target_link_libraries(truffle_shader_compiler_tests PRIVATE truffle_shader)
truffle_target_warnings(truffle_shader_compiler_tests)
add_test(NAME truffle_shader_compiler_tests COMMAND truffle_shader_compiler_tests)
add_executable(truffle_shader_route_tests shader_route_tests.cpp)
target_link_libraries(truffle_shader_route_tests PRIVATE truffle_shader)
truffle_target_warnings(truffle_shader_route_tests)
add_test(NAME truffle_shader_route_tests COMMAND truffle_shader_route_tests)
add_executable(truffle_shader_hlsl_tests shader_hlsl_tests.cpp)
target_link_libraries(truffle_shader_hlsl_tests PRIVATE truffle_shader)
target_compile_definitions(truffle_shader_hlsl_tests PRIVATE
    "TRUFFLE_HLSL_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/shader-hlsl\"")
truffle_target_warnings(truffle_shader_hlsl_tests)
add_test(NAME truffle_shader_hlsl_tests COMMAND truffle_shader_hlsl_tests)
if(TRUFFLE_SHADER_ENABLE_DXC)
    target_compile_definitions(truffle_shader_hlsl_tests PRIVATE TRUFFLE_TEST_DXC=1)
    target_include_directories(truffle_shader_hlsl_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/shader")
    target_link_libraries(truffle_shader_hlsl_tests PRIVATE Truffle::DXC)
endif()
if(TRUFFLE_SHADER_ENABLE_DXC AND TRUFFLE_BUILD_BACKEND_VULKAN)
    add_executable(truffle_shader_hlsl_native_tests shader_hlsl_native_tests.cpp)
    target_link_libraries(truffle_shader_hlsl_native_tests PRIVATE truffle_shader truffle_backend_vulkan)
    target_include_directories(truffle_shader_hlsl_native_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/vulkan")
    target_compile_definitions(truffle_shader_hlsl_native_tests PRIVATE
        "TRUFFLE_HLSL_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/shader-hlsl\"")
    truffle_target_warnings(truffle_shader_hlsl_native_tests)
    if(TRUFFLE_SHADER_ENABLE_SPIRV_CROSS AND TRUFFLE_BUILD_BACKEND_METAL)
        target_link_libraries(truffle_shader_hlsl_native_tests PRIVATE truffle_backend_metal)
        target_compile_definitions(truffle_shader_hlsl_native_tests PRIVATE TRUFFLE_HLSL_TEST_METAL=1)
    endif()
    if(TRUFFLE_SHADER_ENABLE_SPIRV_CROSS AND TRUFFLE_BUILD_BACKEND_OPENGL)
        target_link_libraries(truffle_shader_hlsl_native_tests PRIVATE truffle_backend_opengl)
        target_compile_definitions(truffle_shader_hlsl_native_tests PRIVATE TRUFFLE_HLSL_TEST_OPENGL=1)
    endif()
    add_test(NAME truffle_shader_hlsl_native_tests COMMAND truffle_shader_hlsl_native_tests)
    set_tests_properties(truffle_shader_hlsl_native_tests PROPERTIES
        LABELS "native-smoke;validation;shader"
        FAIL_REGULAR_EXPRESSION "Validation (Error|Warning)"
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")
endif()
add_executable(truffle_shader_portable_tests shader_portable_tests.cpp)
target_link_libraries(truffle_shader_portable_tests PRIVATE truffle_shader)
truffle_target_warnings(truffle_shader_portable_tests)
add_test(NAME truffle_shader_portable_tests COMMAND truffle_shader_portable_tests)
if(TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS)
    add_executable(truffle_shader_spirv_validation_tests shader_spirv_validation_tests.cpp)
    target_include_directories(truffle_shader_spirv_validation_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/shader")
    target_link_libraries(truffle_shader_spirv_validation_tests PRIVATE truffle_shader)
    truffle_target_warnings(truffle_shader_spirv_validation_tests)
    add_test(NAME truffle_shader_spirv_validation_tests COMMAND truffle_shader_spirv_validation_tests)
endif()
if(APPLE AND TRUFFLE_SHADER_ENABLE_SPIRV_CROSS AND TRUFFLE_BUILD_BACKEND_VULKAN AND
   TRUFFLE_BUILD_BACKEND_METAL AND TRUFFLE_BUILD_BACKEND_OPENGL)
    add_executable(truffle_shader_portable_native_tests shader_portable_native_tests.cpp)
    target_link_libraries(truffle_shader_portable_native_tests PRIVATE
        truffle_shader truffle_backend_vulkan truffle_backend_metal truffle_backend_opengl)
    target_include_directories(truffle_shader_portable_native_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/backends/vulkan")
    truffle_target_warnings(truffle_shader_portable_native_tests)
    add_test(NAME truffle_shader_portable_native_tests COMMAND truffle_shader_portable_native_tests)
    set_tests_properties(truffle_shader_portable_native_tests PROPERTIES
        LABELS "native-smoke;validation;shader"
        FAIL_REGULAR_EXPRESSION "Validation (Error|Warning)"
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")
endif()
add_executable(truffle_shader_reflection_tests shader_reflection_tests.cpp)
target_link_libraries(truffle_shader_reflection_tests PRIVATE truffle_shader)
truffle_target_warnings(truffle_shader_reflection_tests)
add_test(NAME truffle_shader_reflection_tests COMMAND truffle_shader_reflection_tests)
if(TRUFFLE_SHADER_ENABLE_GLSLANG)
    target_compile_definitions(truffle_shader_reflection_tests PRIVATE TRUFFLE_TEST_SPIRV_REFLECTION=1)
    target_include_directories(truffle_shader_reflection_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/shader" "${PROJECT_SOURCE_DIR}/vendor/glslang")
endif()
target_link_libraries(truffle_encom_tests PRIVATE truffle_encom)
add_test(NAME truffle_encom_tests COMMAND truffle_encom_tests)

add_executable(truffle_assets_tests asset_declaration_tests.cpp)
target_link_libraries(truffle_assets_tests PRIVATE truffle_assets)
add_test(NAME truffle_assets_tests COMMAND truffle_assets_tests)

add_executable(truffle_rhi_tests rhi_null_tests.cpp)
target_link_libraries(truffle_rhi_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_rhi_tests COMMAND truffle_rhi_tests)

add_executable(truffle_rhi_resource_tests rhi_resource_tests.cpp)
target_link_libraries(truffle_rhi_resource_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_rhi_resource_tests COMMAND truffle_rhi_resource_tests)

add_executable(truffle_rhi_binding_pipeline_tests
               rhi_binding_pipeline_tests.cpp)
target_link_libraries(truffle_rhi_binding_pipeline_tests
                      PRIVATE truffle_backend_null)
add_test(NAME truffle_rhi_binding_pipeline_tests
         COMMAND truffle_rhi_binding_pipeline_tests)

add_executable(truffle_renderer_tests render_flow_tests.cpp)
target_link_libraries(truffle_renderer_tests PRIVATE truffle_backend_null truffle_renderer)
add_test(NAME truffle_renderer_tests COMMAND truffle_renderer_tests)

add_executable(truffle_renderer_program_tests renderer_program_tests.cpp)
target_link_libraries(truffle_renderer_program_tests PRIVATE truffle_backend_null truffle_renderer)
add_test(NAME truffle_renderer_program_tests COMMAND truffle_renderer_program_tests)

# Runs with and without a compiler adapter; disabled builds prove `unsupported`.
add_executable(truffle_renderer_source_program_tests renderer_source_program_tests.cpp)
target_link_libraries(truffle_renderer_source_program_tests PRIVATE truffle_backend_null truffle_renderer)
add_test(NAME truffle_renderer_source_program_tests COMMAND truffle_renderer_source_program_tests)

add_executable(truffle_advanced_render_flow_tests advanced_render_flow_tests.cpp)
target_link_libraries(truffle_advanced_render_flow_tests PRIVATE truffle_backend_null truffle_renderer)
add_test(NAME truffle_advanced_render_flow_tests COMMAND truffle_advanced_render_flow_tests)

add_executable(truffle_render_batch_tests render_batch_tests.cpp)
target_link_libraries(truffle_render_batch_tests PRIVATE truffle_renderer)
add_test(NAME truffle_render_batch_tests COMMAND truffle_render_batch_tests)

add_executable(truffle_asset_render_tests asset_render_plan_tests.cpp)
target_link_libraries(truffle_asset_render_tests PRIVATE truffle_asset_render)
add_test(NAME truffle_asset_render_tests COMMAND truffle_asset_render_tests)

add_executable(truffle_frame_graph_dependency_tests frame_graph_dependency_tests.cpp)
target_link_libraries(truffle_frame_graph_dependency_tests PRIVATE truffle_backend_null truffle_renderer)
add_test(NAME truffle_frame_graph_dependency_tests COMMAND truffle_frame_graph_dependency_tests)

add_executable(truffle_diagnostics_tests diagnostics_tests.cpp)
target_link_libraries(truffle_diagnostics_tests PRIVATE truffle_diagnostics truffle_backend_null)
add_test(NAME truffle_diagnostics_tests COMMAND truffle_diagnostics_tests)

add_executable(truffle_frame_ring_tests frame_ring_tests.cpp)
target_link_libraries(truffle_frame_ring_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_frame_ring_tests COMMAND truffle_frame_ring_tests)

add_executable(truffle_encom_render_adapter_tests encom_render_adapter_tests.cpp)
target_link_libraries(truffle_encom_render_adapter_tests PRIVATE truffle_encom_renderer truffle_backend_null)
add_test(NAME truffle_encom_render_adapter_tests COMMAND truffle_encom_render_adapter_tests)

if(APPLE AND TRUFFLE_BUILD_BACKEND_METAL)
    add_executable(truffle_renderer_metal_tests renderer_metal_tests.cpp)
    target_link_libraries(truffle_renderer_metal_tests PRIVATE truffle_backend_metal truffle_renderer)
    target_compile_definitions(truffle_renderer_metal_tests PRIVATE
        "TRUFFLE_RENDERER_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/renderer\""
        "TRUFFLE_RENDERER_PACKAGE_OUTPUT_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/renderer-native-fixtures\"")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderer-native-fixtures")
    add_test(NAME truffle_renderer_metal_tests COMMAND truffle_renderer_metal_tests)
    set_tests_properties(truffle_renderer_metal_tests PROPERTIES
        LABELS "native-smoke;validation;renderer"
        TIMEOUT 60
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1;METAL_DEVICE_WRAPPER_TYPE=1")
endif()

if(TRUFFLE_BUILD_BACKEND_METAL)
    add_executable(truffle_metal_tests metal_backend_tests.cpp)
    target_link_libraries(truffle_metal_tests PRIVATE truffle_backend_metal truffle_renderer)
    add_test(NAME truffle_metal_tests COMMAND truffle_metal_tests)
    set_tests_properties(truffle_metal_tests PROPERTIES
                         LABELS "native-smoke;validation")
    add_executable(truffle_metal_conformance_tests metal_conformance_tests.cpp)
    target_link_libraries(truffle_metal_conformance_tests PRIVATE truffle_backend_metal)
    add_test(NAME truffle_metal_conformance_tests
             COMMAND truffle_metal_conformance_tests)
    set_tests_properties(truffle_metal_conformance_tests PROPERTIES
                         LABELS "native-smoke;validation")
    add_executable(truffle_metal_presentation_tests metal_presentation_tests.mm)
    target_link_libraries(truffle_metal_presentation_tests
                          PRIVATE truffle_backend_metal "-framework QuartzCore")
    target_include_directories(truffle_metal_presentation_tests
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/metal")
    add_test(NAME truffle_metal_presentation_tests
             COMMAND truffle_metal_presentation_tests)
    set_tests_properties(truffle_metal_presentation_tests PROPERTIES
                         LABELS "presentation;validation")
endif()

if(TRUFFLE_BUILD_BACKEND_VULKAN)
    add_executable(truffle_vulkan_tests vulkan_backend_tests.cpp)
    target_link_libraries(truffle_vulkan_tests PRIVATE truffle_backend_vulkan)
    target_include_directories(truffle_vulkan_tests
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/vulkan")
    if(TRUFFLE_BUILD_SHADERC AND TRUFFLE_SHADER_ENABLE_GLSLANG)
        target_compile_definitions(truffle_vulkan_tests PRIVATE
            "TRUFFLE_VULKAN_SHADER_PACKAGE_PATH=\"${PROJECT_BINARY_DIR}/shaderc-glslang-smoke/triangle-a.tsp\""
            "TRUFFLE_VULKAN_FRAGMENT_PACKAGE_PATH=\"${PROJECT_BINARY_DIR}/shaderc-glslang-smoke/triangle-fragment.tsp\""
            "TRUFFLE_VULKAN_MRT_FRAGMENT_PACKAGE_PATH=\"${PROJECT_BINARY_DIR}/shaderc-glslang-smoke/mrt-fragment.tsp\""
            "TRUFFLE_VULKAN_COMPUTE_COLOR_FRAGMENT_PACKAGE_PATH=\"${PROJECT_BINARY_DIR}/shaderc-glslang-smoke/compute-color-fragment.tsp\""
            "TRUFFLE_VULKAN_TEXTURED_FRAGMENT_PACKAGE_PATH=\"${PROJECT_BINARY_DIR}/shaderc-glslang-smoke/textured-fragment.tsp\""
            "TRUFFLE_VULKAN_COMPUTE_PACKAGE_PATH=\"${PROJECT_BINARY_DIR}/shaderc-glslang-smoke/work-es.tsp\"")
    endif()
    add_test(NAME truffle_vulkan_tests COMMAND truffle_vulkan_tests)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
        find_library(TRUFFLE_XCB_LIBRARY xcb REQUIRED)
        target_link_libraries(truffle_vulkan_tests
                              PRIVATE "${TRUFFLE_XCB_LIBRARY}")
        set_tests_properties(truffle_vulkan_tests PROPERTIES
                             LABELS "native-smoke;presentation;validation")
    elseif(APPLE)
        target_sources(truffle_vulkan_tests PRIVATE vulkan_metal_layer.mm)
        target_link_libraries(truffle_vulkan_tests PRIVATE
                              "-framework Foundation" "-framework QuartzCore")
        # Exit code 77 reports a host without a Vulkan loader/MoltenVK driver.
        set_tests_properties(truffle_vulkan_tests PROPERTIES
                             LABELS "native-smoke;presentation;validation"
                             SKIP_RETURN_CODE 77
                             FAIL_REGULAR_EXPRESSION "Validation (Error|Warning)")
        # Homebrew's validation-layer manifest names its dylib without a path,
        # and dyld does not search the Homebrew prefix. Let the test resolve
        # layers next to the host loader; nothing is pinned or installed.
        find_library(TRUFFLE_VULKAN_HOST_LOADER NAMES vulkan.1 vulkan
                     PATHS /opt/homebrew/lib /usr/local/lib NO_DEFAULT_PATH)
        if(TRUFFLE_VULKAN_HOST_LOADER)
            get_filename_component(_truffle_vulkan_loader_dir
                                   "${TRUFFLE_VULKAN_HOST_LOADER}" DIRECTORY)
            set_property(TEST truffle_vulkan_tests APPEND PROPERTY ENVIRONMENT
                "DYLD_FALLBACK_LIBRARY_PATH=${_truffle_vulkan_loader_dir}:/usr/local/lib:/usr/lib")
            if(TARGET truffle_shader_portable_native_tests)
                set_property(TEST truffle_shader_portable_native_tests APPEND PROPERTY ENVIRONMENT
                    "DYLD_FALLBACK_LIBRARY_PATH=${_truffle_vulkan_loader_dir}:/usr/local/lib:/usr/lib")
            endif()
            if(TARGET truffle_shader_hlsl_native_tests)
                set_property(TEST truffle_shader_hlsl_native_tests APPEND PROPERTY ENVIRONMENT
                    "DYLD_FALLBACK_LIBRARY_PATH=${_truffle_vulkan_loader_dir}:/usr/local/lib:/usr/lib")
            endif()
        endif()
    endif()
    if(TRUFFLE_SHADER_ENABLE_GLSLANG)
        add_executable(truffle_shader_vulkan_reflection_tests shader_vulkan_reflection_tests.cpp)
        target_link_libraries(truffle_shader_vulkan_reflection_tests
                              PRIVATE truffle_backend_vulkan truffle_shader)
        target_include_directories(truffle_shader_vulkan_reflection_tests
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/vulkan")
        truffle_target_warnings(truffle_shader_vulkan_reflection_tests)
        add_test(NAME truffle_shader_vulkan_reflection_tests
                 COMMAND truffle_shader_vulkan_reflection_tests)
        set_tests_properties(truffle_shader_vulkan_reflection_tests PROPERTIES
                             LABELS "native-smoke;validation;shader"
                             SKIP_RETURN_CODE 77
                             FAIL_REGULAR_EXPRESSION "Validation (Error|Warning)")
        if(APPLE AND _truffle_vulkan_loader_dir)
            set_property(TEST truffle_shader_vulkan_reflection_tests APPEND PROPERTY ENVIRONMENT
                "DYLD_FALLBACK_LIBRARY_PATH=${_truffle_vulkan_loader_dir}:/usr/local/lib:/usr/lib")
        endif()
        add_executable(truffle_renderer_vulkan_source_tests renderer_vulkan_source_tests.cpp)
        target_link_libraries(truffle_renderer_vulkan_source_tests
                              PRIVATE truffle_backend_vulkan truffle_renderer)
        target_include_directories(truffle_renderer_vulkan_source_tests
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/vulkan")
        target_compile_definitions(truffle_renderer_vulkan_source_tests PRIVATE
            "TRUFFLE_RENDERER_SOURCE_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/renderer-vulkan-sources\"")
        add_test(NAME truffle_renderer_vulkan_source_tests
                 COMMAND truffle_renderer_vulkan_source_tests)
        set_tests_properties(truffle_renderer_vulkan_source_tests PROPERTIES
                             LABELS "native-smoke;validation;renderer"
                             SKIP_RETURN_CODE 77
                             FAIL_REGULAR_EXPRESSION "Validation (Error|Warning)")
        if(APPLE AND _truffle_vulkan_loader_dir)
            set_property(TEST truffle_renderer_vulkan_source_tests APPEND PROPERTY ENVIRONMENT
                "DYLD_FALLBACK_LIBRARY_PATH=${_truffle_vulkan_loader_dir}:/usr/local/lib:/usr/lib")
        endif()
    endif()
endif()

if(TRUFFLE_BUILD_BACKEND_OPENGL)
    add_executable(truffle_opengl_tests opengl_backend_tests.cpp)
    target_link_libraries(truffle_opengl_tests PRIVATE truffle_backend_opengl)
    if(TRUFFLE_EGL_INCLUDE_DIR AND TRUFFLE_OPENGL_INCLUDE_DIR AND
       TRUFFLE_EGL_LIBRARY AND TRUFFLE_OPENGL_LIBRARY)
        target_compile_definitions(truffle_opengl_tests
                                   PRIVATE TRUFFLE_EXPECT_EGL_OPENGL=1)
    endif()
    if(APPLE AND NOT IOS AND TRUFFLE_OPENGL_FRAMEWORK)
        target_compile_definitions(truffle_opengl_tests
                                   PRIVATE TRUFFLE_EXPECT_CGL_OPENGL=1)
    endif()
    add_test(NAME truffle_opengl_tests COMMAND truffle_opengl_tests)
    if(TRUFFLE_EGL_INCLUDE_DIR AND TRUFFLE_OPENGL_INCLUDE_DIR AND
       TRUFFLE_EGL_LIBRARY AND TRUFFLE_OPENGL_LIBRARY)
        set_tests_properties(truffle_opengl_tests PROPERTIES
                             LABELS "native-smoke;validation")
    elseif(APPLE AND NOT IOS AND TRUFFLE_OPENGL_FRAMEWORK)
        set_tests_properties(truffle_opengl_tests PROPERTIES
                             LABELS "native-smoke")
    endif()
    if(APPLE AND NOT IOS AND TRUFFLE_OPENGL_FRAMEWORK)
        add_executable(truffle_opengl_presentation_tests
                       opengl_presentation_tests.mm)
        target_link_libraries(truffle_opengl_presentation_tests
                              PRIVATE truffle_backend_opengl "-framework AppKit")
        target_compile_definitions(truffle_opengl_presentation_tests
                                   PRIVATE TRUFFLE_EXPECT_CGL_OPENGL=1)
        target_include_directories(truffle_opengl_presentation_tests
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/opengl")
        add_test(NAME truffle_opengl_presentation_tests
                 COMMAND truffle_opengl_presentation_tests)
        set_tests_properties(truffle_opengl_presentation_tests PROPERTIES
                             LABELS "presentation;validation")
    endif()
endif()

if(TRUFFLE_BUILD_BACKEND_OPENGLES)
    add_executable(truffle_opengles_tests opengles_backend_tests.cpp)
    target_link_libraries(truffle_opengles_tests PRIVATE truffle_backend_opengles)
    if(TRUFFLE_EGL_INCLUDE_DIR AND TRUFFLE_OPENGLES_INCLUDE_DIR AND
       TRUFFLE_EGL_LIBRARY AND TRUFFLE_OPENGLES_LIBRARY)
        target_compile_definitions(truffle_opengles_tests
                                   PRIVATE TRUFFLE_EXPECT_EGL_OPENGLES=1)
    endif()
    add_test(NAME truffle_opengles_tests COMMAND truffle_opengles_tests)
    if(TRUFFLE_EGL_INCLUDE_DIR AND TRUFFLE_OPENGLES_INCLUDE_DIR AND
       TRUFFLE_EGL_LIBRARY AND TRUFFLE_OPENGLES_LIBRARY)
        set_tests_properties(truffle_opengles_tests PROPERTIES
                             LABELS "native-smoke;validation")
    endif()
endif()

if(TRUFFLE_BUILD_BACKEND_DIRECT3D)
    add_executable(truffle_direct3d_tests direct3d_backend_tests.cpp)
    target_link_libraries(truffle_direct3d_tests PRIVATE truffle_backend_direct3d)
    add_test(NAME truffle_direct3d_tests COMMAND truffle_direct3d_tests)
    if(WIN32)
        set_tests_properties(truffle_direct3d_tests PROPERTIES
                             LABELS "native-smoke;validation")
        add_executable(truffle_direct3d_presentation_tests
                       direct3d_presentation_tests.cpp)
        target_link_libraries(truffle_direct3d_presentation_tests
                              PRIVATE truffle_backend_direct3d)
        target_include_directories(truffle_direct3d_presentation_tests
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/direct3d")
        add_test(NAME truffle_direct3d_presentation_tests
                 COMMAND truffle_direct3d_presentation_tests)
        set_tests_properties(truffle_direct3d_presentation_tests PROPERTIES
                             LABELS "presentation;validation")
    endif()
endif()

if(TRUFFLE_BUILD_BACKEND_WEBGPU)
    add_executable(truffle_webgpu_tests webgpu_backend_tests.cpp)
    target_link_libraries(truffle_webgpu_tests PRIVATE truffle_backend_webgpu)
    add_test(NAME truffle_webgpu_tests COMMAND truffle_webgpu_tests)
endif()

if(TRUFFLE_BUILD_BACKEND_WEBGL2)
    add_executable(truffle_webgl2_tests webgl2_backend_tests.cpp)
    target_link_libraries(truffle_webgl2_tests PRIVATE truffle_backend_webgl2)
    add_test(NAME truffle_webgl2_tests COMMAND truffle_webgl2_tests)
endif()

add_executable(truffle_backend_support_tests backend_support_tests.cpp)
target_link_libraries(truffle_backend_support_tests PRIVATE truffle_rhi)
add_test(NAME truffle_backend_support_tests COMMAND truffle_backend_support_tests)

add_executable(truffle_rhi_contract_tests rhi_backend_contract_tests.cpp)
target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_null)
if(TRUFFLE_BUILD_BACKEND_VULKAN)
    target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_vulkan)
    target_compile_definitions(truffle_rhi_contract_tests PRIVATE TRUFFLE_HAS_VULKAN_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_METAL)
    target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_metal)
    target_compile_definitions(truffle_rhi_contract_tests PRIVATE TRUFFLE_HAS_METAL_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_OPENGL)
    target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_opengl)
    target_compile_definitions(truffle_rhi_contract_tests PRIVATE TRUFFLE_HAS_OPENGL_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_DIRECT3D)
    target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_direct3d)
    target_compile_definitions(truffle_rhi_contract_tests PRIVATE TRUFFLE_HAS_DIRECT3D_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_OPENGLES)
    target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_opengles)
    target_compile_definitions(truffle_rhi_contract_tests PRIVATE TRUFFLE_HAS_OPENGLES_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_WEBGPU)
    target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_webgpu)
    target_compile_definitions(truffle_rhi_contract_tests PRIVATE TRUFFLE_HAS_WEBGPU_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_WEBGL2)
    target_link_libraries(truffle_rhi_contract_tests PRIVATE truffle_backend_webgl2)
    target_compile_definitions(truffle_rhi_contract_tests PRIVATE TRUFFLE_HAS_WEBGL2_BACKEND=1)
endif()
add_test(NAME truffle_rhi_contract_tests COMMAND truffle_rhi_contract_tests)

add_executable(truffle_rhi_parity_report rhi_parity_report.cpp)
target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_null)
if(TRUFFLE_BUILD_BACKEND_VULKAN)
    target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_vulkan)
    target_compile_definitions(truffle_rhi_parity_report PRIVATE TRUFFLE_HAS_VULKAN_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_METAL)
    target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_metal)
    target_compile_definitions(truffle_rhi_parity_report PRIVATE TRUFFLE_HAS_METAL_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_OPENGL)
    target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_opengl)
    target_compile_definitions(truffle_rhi_parity_report PRIVATE TRUFFLE_HAS_OPENGL_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_DIRECT3D)
    target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_direct3d)
    target_compile_definitions(truffle_rhi_parity_report PRIVATE TRUFFLE_HAS_DIRECT3D_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_OPENGLES)
    target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_opengles)
    target_compile_definitions(truffle_rhi_parity_report PRIVATE TRUFFLE_HAS_OPENGLES_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_WEBGPU)
    target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_webgpu)
    target_compile_definitions(truffle_rhi_parity_report PRIVATE TRUFFLE_HAS_WEBGPU_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_WEBGL2)
    target_link_libraries(truffle_rhi_parity_report PRIVATE truffle_backend_webgl2)
    target_compile_definitions(truffle_rhi_parity_report PRIVATE TRUFFLE_HAS_WEBGL2_BACKEND=1)
endif()
add_test(
    NAME truffle_rhi_parity_report
    COMMAND truffle_rhi_parity_report
            "${PROJECT_BINARY_DIR}/rhi-parity-report.json")

add_executable(truffle_api_version_tests api_version_tests.cpp)
target_link_libraries(truffle_api_version_tests PRIVATE truffle_base)
add_test(NAME truffle_api_version_tests COMMAND truffle_api_version_tests)

add_executable(truffle_base_contract_tests base_contract_tests.cpp)
target_link_libraries(truffle_base_contract_tests PRIVATE truffle_rhi)
add_test(NAME truffle_base_contract_tests COMMAND truffle_base_contract_tests)

add_executable(truffle_shader_package_tests shader_package_tests.cpp)
target_link_libraries(truffle_shader_package_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_shader_package_tests COMMAND truffle_shader_package_tests)

if(TRUFFLE_BUILD_SHADERC)
    if(TRUFFLE_SHADER_ENABLE_DXC)
        add_test(NAME truffle_shaderc_hlsl_smoke COMMAND "${CMAKE_COMMAND}"
            "-DTRUFFLE_SHADERC=$<TARGET_FILE:truffle-shaderc>"
            "-DTRUFFLE_SHADER_EQUIVALENCE=$<TARGET_FILE:truffle_shader_hlsl_tests>"
            "-DTRUFFLE_HLSL_FIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/shader-hlsl"
            "-DTRUFFLE_HLSL_PORTABLE=$<BOOL:${TRUFFLE_SHADER_ENABLE_SPIRV_CROSS}>"
            "-DTRUFFLE_SHADERC_TEST_DIR=${PROJECT_BINARY_DIR}/shaderc-hlsl-smoke"
            -P "${PROJECT_SOURCE_DIR}/cmake/ShadercHlslSmoke.cmake")
    endif()
    if(TRUFFLE_SHADER_ENABLE_SPIRV_CROSS)
        add_executable(truffle_shader_portable_equivalence_tests shader_portable_equivalence_tests.cpp)
        target_link_libraries(truffle_shader_portable_equivalence_tests PRIVATE truffle_shader)
        truffle_target_warnings(truffle_shader_portable_equivalence_tests)
        add_test(NAME truffle_shaderc_portable_smoke COMMAND "${CMAKE_COMMAND}"
            "-DTRUFFLE_SHADERC=$<TARGET_FILE:truffle-shaderc>"
            "-DTRUFFLE_SHADER_EQUIVALENCE=$<TARGET_FILE:truffle_shader_portable_equivalence_tests>"
            "-DTRUFFLE_SHADERC_TEST_DIR=${PROJECT_BINARY_DIR}/shaderc-portable-smoke"
            -P "${PROJECT_SOURCE_DIR}/cmake/ShadercPortableSmoke.cmake")
    endif()
    add_test(NAME truffle_shaderc_routes
        COMMAND "${CMAKE_COMMAND}"
            "-DTRUFFLE_SHADERC=$<TARGET_FILE:truffle-shaderc>"
            "-DTRUFFLE_EXPECT_COMPILER=$<BOOL:${TRUFFLE_SHADER_ENABLE_GLSLANG}>"
            "-DTRUFFLE_EXPECT_DXC=$<BOOL:${TRUFFLE_SHADER_ENABLE_DXC}>"
            "-DTRUFFLE_SHADERC_TEST_DIR=${PROJECT_BINARY_DIR}/shaderc-routes"
            -P "${PROJECT_SOURCE_DIR}/cmake/ShadercRoutesSmoke.cmake")
    add_test(NAME truffle_shaderc_self_test COMMAND truffle-shaderc --self-test)
    add_test(
        NAME truffle_shaderc_package_smoke
        COMMAND
            "${CMAKE_COMMAND}"
            "-DTRUFFLE_SHADERC=$<TARGET_FILE:truffle-shaderc>"
            "-DTRUFFLE_SHADERC_TEST_DIR=${PROJECT_BINARY_DIR}/shaderc-smoke"
            "-DTRUFFLE_EXPECT_COMPILER=$<BOOL:${TRUFFLE_SHADER_ENABLE_GLSLANG}>"
            -P "${PROJECT_SOURCE_DIR}/cmake/ShadercSmoke.cmake")
    if(TRUFFLE_SHADER_ENABLE_GLSLANG)
        add_executable(truffle_shaderc_equivalence_tests shaderc_equivalence_tests.cpp)
        target_link_libraries(truffle_shaderc_equivalence_tests PRIVATE truffle_shader)
        truffle_target_warnings(truffle_shaderc_equivalence_tests)
        add_test(NAME truffle_shaderc_configured_smoke
            COMMAND ${CMAKE_COMMAND}
                "-DTRUFFLE_SHADERC=$<TARGET_FILE:truffle-shaderc>"
                "-DTRUFFLE_SHADER_EQUIVALENCE=$<TARGET_FILE:truffle_shaderc_equivalence_tests>"
                "-DTRUFFLE_SHADERC_TEST_DIR=${PROJECT_BINARY_DIR}/shaderc-configured-smoke"
                -P "${PROJECT_SOURCE_DIR}/cmake/ShadercConfiguredSmoke.cmake")
        add_test(
            NAME truffle_shaderc_glslang_smoke
            COMMAND ${CMAKE_COMMAND}
                "-DTRUFFLE_SHADERC=$<TARGET_FILE:truffle-shaderc>"
                "-DTRUFFLE_SHADERC_TEST_DIR=${PROJECT_BINARY_DIR}/shaderc-glslang-smoke"
                -P "${PROJECT_SOURCE_DIR}/cmake/ShadercGlslangSmoke.cmake")
        set_tests_properties(truffle_shaderc_glslang_smoke PROPERTIES
                             FIXTURES_SETUP vulkan_spirv_package)
        if(TRUFFLE_BUILD_BACKEND_VULKAN)
            set_tests_properties(truffle_vulkan_tests PROPERTIES
                                 FIXTURES_REQUIRED vulkan_spirv_package)
        endif()
    endif()
endif()

add_executable(truffle_performance_sanity_tests performance_sanity_tests.cpp)
target_link_libraries(truffle_performance_sanity_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_performance_sanity_tests COMMAND truffle_performance_sanity_tests)

if(TRUFFLE_INSTALL)
    add_test(NAME truffle_shader_installed_consumer
        COMMAND "${CMAKE_COMMAND}"
            "-DTRUFFLE_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DTRUFFLE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DTRUFFLE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/shader-install-test/install"
            "-DTRUFFLE_CONSUMER_BUILD_DIR=${PROJECT_BINARY_DIR}/shader-install-test/consumer"
            "-DTRUFFLE_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
            "-DTRUFFLE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DTRUFFLE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
            "-DTRUFFLE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            "-DTRUFFLE_EXPECT_COMPILER=$<OR:$<BOOL:${TRUFFLE_SHADER_ENABLE_GLSLANG}>,$<BOOL:${TRUFFLE_SHADER_ENABLE_DXC}>>"
            "-DTRUFFLE_CONFIG=$<CONFIG>"
            -P "${PROJECT_SOURCE_DIR}/cmake/ShaderInstallSmoke.cmake")
    add_test(
        NAME truffle_package_consumer
        COMMAND
            "${CMAKE_COMMAND}"
            "-DTRUFFLE_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DTRUFFLE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DTRUFFLE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/package-test/install"
            "-DTRUFFLE_CONSUMER_BUILD_DIR=${PROJECT_BINARY_DIR}/package-test/consumer"
            "-DTRUFFLE_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
            "-DTRUFFLE_CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DTRUFFLE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            -P "${PROJECT_SOURCE_DIR}/cmake/VerifyInstall.cmake")
endif()

add_executable(truffle_transform_compute_tests transform_compute_tests.cpp)
target_link_libraries(truffle_transform_compute_tests PRIVATE truffle_backend_null truffle_renderer)
add_test(NAME truffle_transform_compute_tests COMMAND truffle_transform_compute_tests)
