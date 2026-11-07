add_library(truffle_renderer STATIC renderer.cpp pipeline_cache.cpp
            transform_compute_pass.cpp source_program.cpp mesh.cpp data_layout.cpp
            view.cpp depth_target.cpp frame_stream.cpp texture.cpp presentation.cpp
            consumer_resources.cpp consumer_program.cpp consumer_frame.cpp consumer_presentation.cpp)
target_link_libraries(truffle_renderer PUBLIC truffle_shader truffle_rhi)
truffle_configure_target(truffle_renderer Renderer)
add_library(Truffle::Renderer ALIAS truffle_renderer)

if(TFL_BUILD_RENDERER_PRESENTATION)
    add_library(truffle_renderer_presentation STATIC presentation_native.cpp consumer_native.cpp)
    target_link_libraries(truffle_renderer_presentation PUBLIC truffle_renderer)
    if(TFL_BUILD_BACKEND_METAL)
        target_link_libraries(truffle_renderer_presentation PRIVATE truffle_backend_metal)
        target_compile_definitions(truffle_renderer_presentation PRIVATE TFL_PRESENTATION_METAL=1)
    endif()
    if(TFL_BUILD_BACKEND_VULKAN)
        target_link_libraries(truffle_renderer_presentation PRIVATE truffle_backend_vulkan)
        target_compile_definitions(truffle_renderer_presentation PRIVATE TFL_PRESENTATION_VULKAN=1)
    endif()
    truffle_configure_target(truffle_renderer_presentation RendererPresentation)
    add_library(Truffle::RendererPresentation ALIAS truffle_renderer_presentation)
endif()
