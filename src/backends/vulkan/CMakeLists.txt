set(_truffle_vulkan_headers_dir "${PROJECT_SOURCE_DIR}/vendor/vulkan-headers")
set(_truffle_volk_dir "${PROJECT_SOURCE_DIR}/vendor/volk")
if(NOT EXISTS "${_truffle_vulkan_headers_dir}/include/vulkan/vulkan.h" OR
   NOT EXISTS "${_truffle_volk_dir}/volk.c")
    message(FATAL_ERROR
        "TRUFFLE_BUILD_BACKEND_VULKAN requires the pinned Vulkan dependency group. "
        "Run: git submodule update --init vendor/vulkan-headers vendor/volk")
endif()

add_library(truffle_backend_vulkan STATIC vulkan_backend.cpp)
target_sources(truffle_backend_vulkan PRIVATE "${_truffle_volk_dir}/volk.c")
set_source_files_properties("${_truffle_volk_dir}/volk.c" PROPERTIES LANGUAGE CXX)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    target_compile_definitions(truffle_backend_vulkan
                               PRIVATE VK_USE_PLATFORM_XCB_KHR=1)
endif()
target_include_directories(truffle_backend_vulkan PRIVATE
    "${PROJECT_SOURCE_DIR}/src/rhi"
    "${_truffle_vulkan_headers_dir}/include"
    "${_truffle_volk_dir}")
target_link_libraries(truffle_backend_vulkan
    PUBLIC truffle_rhi
    PRIVATE ${CMAKE_DL_LIBS})
truffle_configure_target(truffle_backend_vulkan BackendVulkan)
add_library(Truffle::BackendVulkan ALIAS truffle_backend_vulkan)
