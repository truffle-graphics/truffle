add_library(truffle_gui STATIC builder.cpp context.cpp draw.cpp events.cpp layout.cpp painter.cpp placement.cpp region.cpp shapes.cpp text.cpp theme.cpp updates.cpp widgets.cpp)
target_link_libraries(truffle_gui PUBLIC truffle_renderer)
truffle_configure_target(truffle_gui GUI)
add_library(Truffle::GUI ALIAS truffle_gui)

if(TFL_GUI_ENABLE_TEXT)
    foreach(_dependency freetype harfbuzz)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/${_dependency}/CMakeLists.txt")
            message(FATAL_ERROR
                "GUI text needs the pinned vendor/${_dependency} source; prepare it with "
                "`git submodule update --init vendor/freetype vendor/harfbuzz`, or "
                "configure a text-free GUI with -DTFL_GUI_ENABLE_TEXT=OFF")
        endif()
    endforeach()

    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
    set(_truffle_gui_text_exclude_from_all)
    if(NOT TFL_INSTALL)
        set(_truffle_gui_text_exclude_from_all EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory("${PROJECT_SOURCE_DIR}/vendor/freetype"
                     "${PROJECT_BINARY_DIR}/vendor/freetype"
                     ${_truffle_gui_text_exclude_from_all})

    set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
    set(HB_HAVE_CORETEXT OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_UNISCRIBE OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GDI OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_DIRECTWRITE OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/vendor/harfbuzz"
                     "${PROJECT_BINARY_DIR}/vendor/harfbuzz"
                     ${_truffle_gui_text_exclude_from_all})

    add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    target_compile_definitions(truffle_gui PRIVATE TFL_GUI_HAS_TEXT=1)
    target_link_libraries(truffle_gui PRIVATE freetype harfbuzz::harfbuzz)
    if(TFL_INSTALL)
        include(GNUInstallDirs)
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/freetype/docs/FTL.TXT"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle"
                RENAME FreeType-FTL.TXT)
        install(FILES "${PROJECT_SOURCE_DIR}/docs/licenses/FreeType-attribution.txt"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle")
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/harfbuzz/COPYING"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle"
                RENAME HarfBuzz-COPYING)
    endif()
else()
    target_compile_definitions(truffle_gui PRIVATE TFL_GUI_HAS_TEXT=0)
endif()

# GUI shaders ship as prepared native source: the package is built in memory
# from the embedded source and its hash, so no shader compiler runs.
set(_truffle_gui_metal "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gui.metal")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_truffle_gui_metal}")
file(READ "${_truffle_gui_metal}" TFL_GUI_METAL_SOURCE)
file(SHA256 "${_truffle_gui_metal}" TFL_GUI_METAL_SHA256)
configure_file(gui_shaders.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/truffle_gui_shaders.hpp" @ONLY)
target_include_directories(truffle_gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
