# The GUI program ships as prepared MSL, so no shader compiler is required.
if(APPLE AND TRUFFLE_BUILD_BACKEND_METAL)
    add_executable(truffle_gui_window main.mm)
    target_link_libraries(truffle_gui_window PRIVATE truffle_gui truffle_backend_metal
        "-framework AppKit" "-framework QuartzCore")
    target_compile_options(truffle_gui_window PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    truffle_target_warnings(truffle_gui_window)
    if(TRUFFLE_BUILD_TESTS)
        add_test(NAME truffle_gui_window COMMAND truffle_gui_window --smoke)
        set_tests_properties(truffle_gui_window PROPERTIES
            LABELS "native-smoke;gui;presentation;host-window;validation"
            TIMEOUT 60
            ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1;METAL_DEVICE_WRAPPER_TYPE=1")
    endif()
endif()
