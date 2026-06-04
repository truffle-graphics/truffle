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

function(truffle_defer_backend option backend)
    if(${option})
        message(FATAL_ERROR
            "${backend} is reserved by ${option}, but its Truffle backend "
            "implementation has not landed yet.")
    endif()
endfunction()

truffle_defer_backend(TRUFFLE_BUILD_BACKEND_DIRECT3D "Direct3D")
