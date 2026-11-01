add_library(truffle_gui_dock STATIC dock.cpp layout.cpp)
target_link_libraries(truffle_gui_dock PUBLIC truffle_gui)
truffle_configure_target(truffle_gui_dock GUIDock)
add_library(Truffle::GUIDock ALIAS truffle_gui_dock)
