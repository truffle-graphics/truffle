add_executable(truffle_dense_diagnostics_example src/main.cpp)
target_link_libraries(truffle_dense_diagnostics_example PRIVATE truffle_diagnostics)

if(TRUFFLE_BUILD_TESTS)
    add_test(
        NAME truffle_dense_diagnostics_smoke
        COMMAND truffle_dense_diagnostics_example --smoke)
endif()
