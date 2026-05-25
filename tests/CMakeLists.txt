add_executable(truffle_ecs_tests ecs_contract_tests.cpp)
target_link_libraries(truffle_ecs_tests PRIVATE truffle_ecs)
add_test(NAME truffle_ecs_tests COMMAND truffle_ecs_tests)

add_executable(truffle_rhi_tests rhi_null_tests.cpp)
target_link_libraries(truffle_rhi_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_rhi_tests COMMAND truffle_rhi_tests)

add_executable(truffle_render_tests render_flow_tests.cpp)
target_link_libraries(truffle_render_tests PRIVATE truffle_backend_null truffle_render)
add_test(NAME truffle_render_tests COMMAND truffle_render_tests)

add_executable(truffle_render_batch_tests render_batch_tests.cpp)
target_link_libraries(truffle_render_batch_tests PRIVATE truffle_render)
add_test(NAME truffle_render_batch_tests COMMAND truffle_render_batch_tests)

add_executable(truffle_frame_ring_tests frame_ring_tests.cpp)
target_link_libraries(truffle_frame_ring_tests PRIVATE truffle_backend_null)
add_test(NAME truffle_frame_ring_tests COMMAND truffle_frame_ring_tests)

add_executable(truffle_scene_adapter_tests scene_adapter_tests.cpp)
target_link_libraries(truffle_scene_adapter_tests PRIVATE truffle_scene truffle_backend_null)
add_test(NAME truffle_scene_adapter_tests COMMAND truffle_scene_adapter_tests)

if(TRUFFLE_BUILD_BACKEND_METAL)
    add_executable(truffle_metal_tests metal_backend_tests.cpp)
    target_link_libraries(truffle_metal_tests PRIVATE truffle_backend_metal)
    add_test(NAME truffle_metal_tests COMMAND truffle_metal_tests)
endif()

if(TRUFFLE_INSTALL)
    add_test(
        NAME truffle_package_consumer
        COMMAND
            "${CMAKE_COMMAND}"
            "-DTRUFFLE_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DTRUFFLE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DTRUFFLE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/package-test/install"
            "-DTRUFFLE_CONSUMER_BUILD_DIR=${PROJECT_BINARY_DIR}/package-test/consumer"
            -P "${PROJECT_SOURCE_DIR}/cmake/VerifyInstall.cmake")
endif()

add_executable(truffle_transform_compute_tests transform_compute_tests.cpp)
target_link_libraries(truffle_transform_compute_tests PRIVATE truffle_backend_null truffle_render)
add_test(NAME truffle_transform_compute_tests COMMAND truffle_transform_compute_tests)
