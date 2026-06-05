add_library(truffle_render STATIC renderer.cpp transform_compute_pass.cpp)
target_link_libraries(truffle_render PUBLIC truffle_rhi)
truffle_configure_target(truffle_render Render)
add_library(Truffle::Render ALIAS truffle_render)
