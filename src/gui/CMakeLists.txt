add_library(truffle_gui STATIC graphics.cpp graphics_resources.cpp builder.cpp collections.cpp context.cpp draw.cpp events.cpp layout.cpp menu_session.cpp menus.cpp painter.cpp placement.cpp region.cpp shapes.cpp surface.cpp text.cpp text_edit.cpp text_layout.cpp text_unicode.cpp theme.cpp themes.cpp updates.cpp viewport.cpp viewport_rendering.cpp widgets.cpp)
target_link_libraries(truffle_gui PUBLIC truffle_renderer)
truffle_configure_target(truffle_gui GUI)
add_library(Truffle::GUI ALIAS truffle_gui)

if(TARGET truffle_renderer_presentation)
    target_link_libraries(truffle_gui PRIVATE truffle_renderer_presentation)
    target_compile_definitions(truffle_gui PRIVATE TFL_GUI_NATIVE_PRESENTATION=1)
else()
    target_compile_definitions(truffle_gui PRIVATE TFL_GUI_NATIVE_PRESENTATION=0)
endif()

if(TFL_GUI_ENABLE_TEXT)
    enable_language(C)
    foreach(_dependency freetype harfbuzz)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/${_dependency}/CMakeLists.txt")
            message(FATAL_ERROR
                "GUI text needs the pinned vendor/${_dependency} source; prepare it with "
                "`git submodule update --init vendor/freetype vendor/harfbuzz`, or "
                "configure a text-free GUI with -DTFL_GUI_ENABLE_TEXT=OFF")
        endif()
    endforeach()

    foreach(_source sheenbidi/Source/SheenBidi.c utf8proc/utf8proc.c)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/${_source}")
            message(FATAL_ERROR
                "GUI text needs pinned Unicode sources; run "
                "`git submodule update --init vendor/sheenbidi vendor/utf8proc`, "
                "or set -DTFL_GUI_ENABLE_TEXT=OFF")
        endif()
    endforeach()
    # Compile only the upstream runtime sources: no tools, tests, network or
    # global upstream CMake options. These private static dependencies travel
    # in the same install/export set as GUI.
    add_library(truffle_gui_sheenbidi STATIC
        "${PROJECT_SOURCE_DIR}/vendor/sheenbidi/Source/SheenBidi.c")
    target_compile_definitions(truffle_gui_sheenbidi PRIVATE SB_CONFIG_UNITY)
    target_include_directories(truffle_gui_sheenbidi SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/vendor/sheenbidi/Headers>")
    add_library(truffle_gui_utf8proc STATIC
        "${PROJECT_SOURCE_DIR}/vendor/utf8proc/utf8proc.c")
    target_compile_definitions(truffle_gui_utf8proc PUBLIC UTF8PROC_STATIC)
    target_include_directories(truffle_gui_utf8proc SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/vendor/utf8proc>")
    set_target_properties(truffle_gui_sheenbidi truffle_gui_utf8proc PROPERTIES
        POSITION_INDEPENDENT_CODE ON C_STANDARD 99 C_STANDARD_REQUIRED ON)
    set_target_properties(truffle_gui_sheenbidi PROPERTIES EXPORT_NAME GUIUnicodeBidi)
    set_target_properties(truffle_gui_utf8proc PROPERTIES EXPORT_NAME GUIUnicodeGraphemes)
    target_link_libraries(truffle_gui PRIVATE truffle_gui_sheenbidi truffle_gui_utf8proc)
    if(TFL_INSTALL)
        include(GNUInstallDirs)
        install(TARGETS truffle_gui_sheenbidi truffle_gui_utf8proc
            EXPORT TruffleTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/sheenbidi/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle"
            RENAME SheenBidi-LICENSE)
        install(FILES "${PROJECT_SOURCE_DIR}/docs/licenses/SheenBidi-attribution.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle")
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/utf8proc/LICENSE.md"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle"
            RENAME utf8proc-LICENSE.md)
        install(FILES "${PROJECT_SOURCE_DIR}/docs/licenses/Unicode-3.0.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle")
    endif()

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

# Prepared Metal source and Vulkan SPIR-V need no compiler at configure or runtime.
set(_truffle_gui_metal "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gui.metal")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_truffle_gui_metal}")
file(READ "${_truffle_gui_metal}" TFL_GUI_METAL_SOURCE)
file(SHA256 "${_truffle_gui_metal}" TFL_GUI_METAL_SHA256)
set(_truffle_gui_vulkan "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gui-vulkan.tsp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_truffle_gui_vulkan}")
file(READ "${_truffle_gui_vulkan}" _truffle_gui_vulkan_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," TFL_GUI_VULKAN_BYTES "${_truffle_gui_vulkan_hex}")
configure_file(gui_shaders.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/truffle_gui_shaders.hpp" @ONLY)
target_include_directories(truffle_gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
