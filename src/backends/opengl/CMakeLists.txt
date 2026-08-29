add_library(truffle_backend_opengl STATIC opengl_backend.cpp)
target_include_directories(truffle_backend_opengl PRIVATE "${PROJECT_SOURCE_DIR}/src/rhi")
target_link_libraries(truffle_backend_opengl PUBLIC truffle_rhi)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    find_path(TRUFFLE_EGL_INCLUDE_DIR EGL/egl.h)
    find_path(TRUFFLE_OPENGL_INCLUDE_DIR GL/gl.h)
    find_library(TRUFFLE_EGL_LIBRARY EGL)
    find_library(TRUFFLE_OPENGL_LIBRARY OpenGL)
    if(TRUFFLE_EGL_INCLUDE_DIR AND TRUFFLE_OPENGL_INCLUDE_DIR AND
       TRUFFLE_EGL_LIBRARY AND TRUFFLE_OPENGL_LIBRARY)
        target_compile_definitions(truffle_backend_opengl
                                   PRIVATE TRUFFLE_HAS_EGL_OPENGL=1)
        target_include_directories(truffle_backend_opengl PRIVATE
                                   "${TRUFFLE_EGL_INCLUDE_DIR}"
                                   "${TRUFFLE_OPENGL_INCLUDE_DIR}")
        target_link_libraries(truffle_backend_opengl PRIVATE
                              "${TRUFFLE_EGL_LIBRARY}"
                              "${TRUFFLE_OPENGL_LIBRARY}")
    else()
        message(STATUS
            "OpenGL target is source-only: EGL/OpenGL development files were not found")
    endif()
endif()
truffle_configure_target(truffle_backend_opengl BackendOpenGL)
add_library(Truffle::BackendOpenGL ALIAS truffle_backend_opengl)
