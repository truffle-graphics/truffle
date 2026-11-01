add_library(truffle_backend_opengles STATIC opengles_backend.cpp)
target_include_directories(truffle_backend_opengles PRIVATE
                           "${PROJECT_SOURCE_DIR}/src/rhi"
                           "${PROJECT_SOURCE_DIR}/src/backends/opengl")
target_link_libraries(truffle_backend_opengles PUBLIC truffle_rhi)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    find_path(TFL_EGL_INCLUDE_DIR EGL/egl.h)
    find_path(TFL_OPENGLES_INCLUDE_DIR GLES3/gl3.h)
    find_library(TFL_EGL_LIBRARY EGL)
    find_library(TFL_OPENGLES_LIBRARY GLESv2)
    if(TFL_EGL_INCLUDE_DIR AND TFL_OPENGLES_INCLUDE_DIR AND
       TFL_EGL_LIBRARY AND TFL_OPENGLES_LIBRARY)
        target_compile_definitions(truffle_backend_opengles
                                   PRIVATE TFL_HAS_EGL_OPENGLES=1)
        target_include_directories(truffle_backend_opengles PRIVATE
                                   "${TFL_EGL_INCLUDE_DIR}"
                                   "${TFL_OPENGLES_INCLUDE_DIR}")
        target_link_libraries(truffle_backend_opengles PRIVATE
                              "${TFL_EGL_LIBRARY}"
                              "${TFL_OPENGLES_LIBRARY}")
    else()
        message(STATUS
            "OpenGL ES target is source-only: EGL/GLES3 development files were not found")
    endif()
endif()
truffle_configure_target(truffle_backend_opengles BackendOpenGLES)
add_library(Truffle::BackendOpenGLES ALIAS truffle_backend_opengles)
