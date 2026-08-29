if(TRUFFLE_BUILD_BACKEND_NULL)
    add_subdirectory(null)
endif()

if(TRUFFLE_BUILD_BACKEND_METAL)
    if(NOT APPLE)
        message(FATAL_ERROR "TRUFFLE_BUILD_BACKEND_METAL requires an Apple platform")
    endif()
    add_subdirectory(metal)
endif()

if(TRUFFLE_BUILD_BACKEND_VULKAN)
    add_subdirectory(vulkan)
endif()

if(TRUFFLE_BUILD_BACKEND_OPENGL)
    add_subdirectory(opengl)
endif()

if(TRUFFLE_BUILD_BACKEND_OPENGLES)
    add_subdirectory(opengles)
endif()

if(TRUFFLE_BUILD_BACKEND_WEBGPU)
    add_subdirectory(webgpu)
endif()

if(TRUFFLE_BUILD_BACKEND_WEBGL2)
    add_subdirectory(webgl2)
endif()

if(TRUFFLE_BUILD_BACKEND_DIRECT3D)
    add_subdirectory(direct3d)
endif()
