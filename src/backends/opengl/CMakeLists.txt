add_library(truffle_backend_opengl STATIC opengl_backend.cpp)
target_include_directories(truffle_backend_opengl PRIVATE "${PROJECT_SOURCE_DIR}/src/rhi")
target_link_libraries(truffle_backend_opengl PUBLIC truffle_rhi)
if(WIN32)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/opengl-registry/api/GL/glcorearb.h" OR
       NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/egl-registry/api/KHR/khrplatform.h")
        message(FATAL_ERROR
            "Windows OpenGL requires pinned header dependencies. Run: "
            "git submodule update --init vendor/opengl-registry vendor/egl-registry")
    endif()
    target_compile_definitions(truffle_backend_opengl PRIVATE
                               TFL_HAS_WGL_OPENGL=1 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(truffle_backend_opengl SYSTEM PRIVATE
        "${PROJECT_SOURCE_DIR}/vendor/opengl-registry/api"
        "${PROJECT_SOURCE_DIR}/vendor/egl-registry/api")
    target_link_libraries(truffle_backend_opengl PRIVATE opengl32 gdi32 user32)
elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    find_path(TFL_EGL_INCLUDE_DIR EGL/egl.h)
    find_path(TFL_OPENGL_INCLUDE_DIR GL/gl.h)
    find_library(TFL_EGL_LIBRARY EGL)
    find_library(TFL_OPENGL_LIBRARY OpenGL)
    if(TFL_EGL_INCLUDE_DIR AND TFL_OPENGL_INCLUDE_DIR AND
       TFL_EGL_LIBRARY AND TFL_OPENGL_LIBRARY)
        target_compile_definitions(truffle_backend_opengl
                                   PRIVATE TFL_HAS_EGL_OPENGL=1)
        target_include_directories(truffle_backend_opengl PRIVATE
                                   "${TFL_EGL_INCLUDE_DIR}"
                                   "${TFL_OPENGL_INCLUDE_DIR}")
        target_link_libraries(truffle_backend_opengl PRIVATE
                              "${TFL_EGL_LIBRARY}"
                              "${TFL_OPENGL_LIBRARY}")
    else()
        message(STATUS
            "OpenGL target is source-only: EGL/OpenGL development files were not found")
    endif()
elseif(APPLE AND NOT IOS)
    find_library(TFL_OPENGL_FRAMEWORK OpenGL)
    if(TFL_OPENGL_FRAMEWORK)
        # macOS deprecates OpenGL; silence only those warnings for this target.
        target_compile_definitions(truffle_backend_opengl PRIVATE
                                   TFL_HAS_CGL_OPENGL=1
                                   GL_SILENCE_DEPRECATION=1)
        # Host-owned NSView presentation needs AppKit's NSOpenGLContext.
        target_sources(truffle_backend_opengl PRIVATE cgl_view.mm)
        target_link_libraries(truffle_backend_opengl PRIVATE
                              "${TFL_OPENGL_FRAMEWORK}"
                              "-framework AppKit")
    else()
        message(STATUS
            "OpenGL target is source-only: OpenGL.framework was not found")
    endif()
endif()
truffle_configure_target(truffle_backend_opengl BackendOpenGL)
add_library(Truffle::BackendOpenGL ALIAS truffle_backend_opengl)
