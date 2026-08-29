add_library(truffle_backend_webgpu STATIC webgpu_backend.cpp)
target_include_directories(truffle_backend_webgpu PRIVATE
                           "${PROJECT_SOURCE_DIR}/src/rhi")
target_link_libraries(truffle_backend_webgpu PUBLIC truffle_rhi)
truffle_configure_target(truffle_backend_webgpu BackendWebGPU)
add_library(Truffle::BackendWebGPU ALIAS truffle_backend_webgpu)
