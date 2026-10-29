add_library(truffle_encom STATIC world.cpp)
target_link_libraries(truffle_encom PUBLIC truffle_base)
truffle_configure_target(truffle_encom EnCom)
add_library(Truffle::EnCom ALIAS truffle_encom)
