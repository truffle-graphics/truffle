add_library(truffle_assets STATIC assets.cpp)
target_link_libraries(truffle_assets PUBLIC truffle_core)
truffle_configure_target(truffle_assets Assets)
add_library(Truffle::Assets ALIAS truffle_assets)
