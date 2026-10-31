add_library(truffle_gui STATIC context.cpp draw.cpp layout.cpp painter.cpp shapes.cpp)
target_link_libraries(truffle_gui PUBLIC truffle_renderer)
truffle_configure_target(truffle_gui GUI)
add_library(Truffle::GUI ALIAS truffle_gui)

# GUI shaders ship as prepared native source: the package is built in memory
# from the embedded source and its hash, so no shader compiler runs.
set(_truffle_gui_metal "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gui.metal")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_truffle_gui_metal}")
file(READ "${_truffle_gui_metal}" TRUFFLE_GUI_METAL_SOURCE)
file(SHA256 "${_truffle_gui_metal}" TRUFFLE_GUI_METAL_SHA256)
configure_file(gui_shaders.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/truffle_gui_shaders.hpp" @ONLY)
target_include_directories(truffle_gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
