add_library(truffle_rhi STATIC backend_support.cpp rhi.cpp shader_package.cpp)
target_link_libraries(truffle_rhi PUBLIC truffle_base Threads::Threads)
truffle_configure_target(truffle_rhi RHI)
add_library(Truffle::RHI ALIAS truffle_rhi)
