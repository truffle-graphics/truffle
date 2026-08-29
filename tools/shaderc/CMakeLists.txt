add_executable(truffle-shaderc main.cpp)
target_link_libraries(truffle-shaderc PRIVATE truffle_rhi)
target_compile_features(truffle-shaderc PRIVATE cxx_std_20)
truffle_target_warnings(truffle-shaderc)

if(TRUFFLE_INSTALL)
    install(TARGETS truffle-shaderc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
