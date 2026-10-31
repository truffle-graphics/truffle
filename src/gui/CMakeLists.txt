add_library(truffle_gui STATIC context.cpp)
target_link_libraries(truffle_gui PUBLIC truffle_renderer)
truffle_configure_target(truffle_gui GUI)
add_library(Truffle::GUI ALIAS truffle_gui)
