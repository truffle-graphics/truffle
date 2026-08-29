add_library(truffle_backend_webgl2 STATIC webgl2_backend.cpp)
target_include_directories(truffle_backend_webgl2 PRIVATE
                           "${PROJECT_SOURCE_DIR}/src/rhi")
target_link_libraries(truffle_backend_webgl2 PUBLIC truffle_rhi)
if(EMSCRIPTEN)
    target_compile_definitions(truffle_backend_webgl2
                               PRIVATE TRUFFLE_HAS_WEBGL2_NATIVE=1)
    target_compile_options(truffle_backend_webgl2 PRIVATE -sFULL_ES3=1)
    target_link_options(truffle_backend_webgl2 PRIVATE
                        -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)
endif()
truffle_configure_target(truffle_backend_webgl2 BackendWebGL2)
add_library(Truffle::BackendWebGL2 ALIAS truffle_backend_webgl2)
