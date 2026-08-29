add_executable(truffle-rhi-doctor main.cpp)
target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_null)
if(TRUFFLE_BUILD_BACKEND_METAL)
    target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_metal)
    target_compile_definitions(truffle-rhi-doctor PRIVATE
                               TRUFFLE_HAS_METAL_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_VULKAN)
    target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_vulkan)
    target_compile_definitions(truffle-rhi-doctor PRIVATE
                               TRUFFLE_HAS_VULKAN_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_DIRECT3D)
    target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_direct3d)
    target_compile_definitions(truffle-rhi-doctor PRIVATE
                               TRUFFLE_HAS_DIRECT3D_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_OPENGL)
    target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_opengl)
    target_compile_definitions(truffle-rhi-doctor PRIVATE
                               TRUFFLE_HAS_OPENGL_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_OPENGLES)
    target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_opengles)
    target_compile_definitions(truffle-rhi-doctor PRIVATE
                               TRUFFLE_HAS_OPENGLES_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_WEBGPU)
    target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_webgpu)
    target_compile_definitions(truffle-rhi-doctor PRIVATE
                               TRUFFLE_HAS_WEBGPU_BACKEND=1)
endif()
if(TRUFFLE_BUILD_BACKEND_WEBGL2)
    target_link_libraries(truffle-rhi-doctor PRIVATE truffle_backend_webgl2)
    target_compile_definitions(truffle-rhi-doctor PRIVATE
                               TRUFFLE_HAS_WEBGL2_BACKEND=1)
endif()
target_compile_features(truffle-rhi-doctor PRIVATE cxx_std_20)
truffle_target_warnings(truffle-rhi-doctor)
