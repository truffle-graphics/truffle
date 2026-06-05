add_library(truffle_backend_metal STATIC metal_backend.mm)
target_link_libraries(truffle_backend_metal
    PUBLIC  truffle_rhi
    PRIVATE "-framework Metal"
    PRIVATE "-framework QuartzCore"
    PRIVATE "-framework Foundation"
)
truffle_configure_target(truffle_backend_metal BackendMetal)
add_library(Truffle::BackendMetal ALIAS truffle_backend_metal)
