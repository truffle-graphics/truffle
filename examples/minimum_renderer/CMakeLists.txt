if(APPLE AND TRUFFLE_BUILD_BACKEND_METAL AND TRUFFLE_SHADER_ENABLE_METAL_COMPILER)
    add_executable(truffle_minimum_renderer main.mm scene.cpp)
    target_link_libraries(truffle_minimum_renderer PRIVATE truffle_renderer
        truffle_backend_metal "-framework AppKit" "-framework QuartzCore")
    target_compile_options(truffle_minimum_renderer PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_compile_definitions(truffle_minimum_renderer PRIVATE
        "TRUFFLE_MINIMUM_RENDERER_SHADER_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\"")
    truffle_target_warnings(truffle_minimum_renderer)
    if(TRUFFLE_BUILD_TESTS)
        add_test(NAME truffle_minimum_renderer_window
            COMMAND truffle_minimum_renderer --smoke)
        set_tests_properties(truffle_minimum_renderer_window PROPERTIES
            LABELS "native-smoke;renderer;presentation;host-window;validation"
            TIMEOUT 60
            ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1;METAL_DEVICE_WRAPPER_TYPE=1")
    endif()
endif()
