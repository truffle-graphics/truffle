add_library(truffle_renderer STATIC renderer.cpp pipeline_cache.cpp
            transform_compute_pass.cpp source_program.cpp mesh.cpp data_layout.cpp
            view.cpp depth_target.cpp)
target_link_libraries(truffle_renderer PUBLIC truffle_shader truffle_rhi)
truffle_configure_target(truffle_renderer Renderer)
add_library(Truffle::Renderer ALIAS truffle_renderer)
