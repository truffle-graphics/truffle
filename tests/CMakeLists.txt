add_executable(truffle_ecs_tests ecs_contract_tests.cpp)
target_link_libraries(truffle_ecs_tests PRIVATE truffle_ecs)
add_test(NAME truffle_ecs_tests COMMAND truffle_ecs_tests)

add_executable(truffle_assets_tests asset_declaration_tests.cpp)
target_link_libraries(truffle_assets_tests PRIVATE truffle_assets)
add_test(NAME truffle_assets_tests COMMAND truffle_assets_tests)

add_executable(truffle_rhi_tests rhi_null_tests.cpp)
target_link_libraries(truffle_rhi_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_rhi_tests COMMAND truffle_rhi_tests)

add_executable(truffle_rhi_resource_tests rhi_resource_tests.cpp)
target_link_libraries(truffle_rhi_resource_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_rhi_resource_tests COMMAND truffle_rhi_resource_tests)

add_executable(truffle_render_tests render_flow_tests.cpp)
target_link_libraries(truffle_render_tests PRIVATE truffle_backend_null truffle_render)
add_test(NAME truffle_render_tests COMMAND truffle_render_tests)

add_executable(truffle_advanced_render_flow_tests advanced_render_flow_tests.cpp)
target_link_libraries(truffle_advanced_render_flow_tests PRIVATE truffle_backend_null truffle_render)
add_test(NAME truffle_advanced_render_flow_tests COMMAND truffle_advanced_render_flow_tests)

add_executable(truffle_render_batch_tests render_batch_tests.cpp)
target_link_libraries(truffle_render_batch_tests PRIVATE truffle_render)
add_test(NAME truffle_render_batch_tests COMMAND truffle_render_batch_tests)

add_executable(truffle_asset_render_tests asset_render_plan_tests.cpp)
target_link_libraries(truffle_asset_render_tests PRIVATE truffle_asset_render)
add_test(NAME truffle_asset_render_tests COMMAND truffle_asset_render_tests)

add_executable(truffle_frame_graph_dependency_tests frame_graph_dependency_tests.cpp)
target_link_libraries(truffle_frame_graph_dependency_tests PRIVATE truffle_backend_null truffle_render)
add_test(NAME truffle_frame_graph_dependency_tests COMMAND truffle_frame_graph_dependency_tests)

add_executable(truffle_diagnostics_tests diagnostics_tests.cpp)
target_link_libraries(truffle_diagnostics_tests PRIVATE truffle_diagnostics truffle_backend_null)
add_test(NAME truffle_diagnostics_tests COMMAND truffle_diagnostics_tests)

add_executable(truffle_frame_ring_tests frame_ring_tests.cpp)
target_link_libraries(truffle_frame_ring_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_frame_ring_tests COMMAND truffle_frame_ring_tests)

add_executable(truffle_scene_adapter_tests scene_adapter_tests.cpp)
target_link_libraries(truffle_scene_adapter_tests PRIVATE truffle_scene truffle_backend_null)
add_test(NAME truffle_scene_adapter_tests COMMAND truffle_scene_adapter_tests)

if(TRUFFLE_BUILD_BACKEND_METAL)
    add_executable(truffle_metal_tests metal_backend_tests.cpp)
    target_link_libraries(truffle_metal_tests PRIVATE truffle_backend_metal truffle_render)
    add_test(NAME truffle_metal_tests COMMAND truffle_metal_tests)
endif()

if(TRUFFLE_BUILD_BACKEND_VULKAN)
    add_executable(truffle_vulkan_tests vulkan_backend_tests.cpp)
    target_link_libraries(truffle_vulkan_tests PRIVATE truffle_backend_vulkan)
    add_test(NAME truffle_vulkan_tests COMMAND truffle_vulkan_tests)
endif()

if(TRUFFLE_BUILD_BACKEND_OPENGL)
    add_executable(truffle_opengl_tests opengl_backend_tests.cpp)
    target_link_libraries(truffle_opengl_tests PRIVATE truffle_backend_opengl)
    add_test(NAME truffle_opengl_tests COMMAND truffle_opengl_tests)

endif()

if(TRUFFLE_BUILD_BACKEND_DIRECT3D)
    add_executable(truffle_direct3d_tests direct3d_backend_tests.cpp)
    target_link_libraries(truffle_direct3d_tests PRIVATE truffle_backend_direct3d)
    add_test(NAME truffle_direct3d_tests COMMAND truffle_direct3d_tests)
endif()

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
add_test(
    NAME truffle_rhi_parity_report
    COMMAND truffle_rhi_parity_report
            "${PROJECT_BINARY_DIR}/rhi-parity-report.json")

add_executable(truffle_api_version_tests api_version_tests.cpp)
target_link_libraries(truffle_api_version_tests PRIVATE truffle_core)
add_test(NAME truffle_api_version_tests COMMAND truffle_api_version_tests)

add_executable(truffle_core_contract_tests core_contract_tests.cpp)
target_link_libraries(truffle_core_contract_tests PRIVATE truffle_rhi)
add_test(NAME truffle_core_contract_tests COMMAND truffle_core_contract_tests)

add_executable(truffle_performance_sanity_tests performance_sanity_tests.cpp)
target_link_libraries(truffle_performance_sanity_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_performance_sanity_tests COMMAND truffle_performance_sanity_tests)

if(TRUFFLE_INSTALL)
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
target_link_libraries(truffle_transform_compute_tests PRIVATE truffle_backend_null truffle_render)
add_test(NAME truffle_transform_compute_tests COMMAND truffle_transform_compute_tests)
