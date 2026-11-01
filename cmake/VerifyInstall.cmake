if(NOT DEFINED TFL_BINARY_DIR OR
   NOT DEFINED TFL_SOURCE_DIR OR
   NOT DEFINED TFL_INSTALL_PREFIX OR
   NOT DEFINED TFL_CONSUMER_BUILD_DIR)
    message(FATAL_ERROR "Truffle install verification requires build and source paths.")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${TFL_BINARY_DIR}"
        --prefix "${TFL_INSTALL_PREFIX}"
    COMMAND_ERROR_IS_FATAL ANY)

set(_truffle_consumer_configure
    "${CMAKE_COMMAND}"
    -S "${TFL_SOURCE_DIR}/tests/package_consumer"
    -B "${TFL_CONSUMER_BUILD_DIR}"
    "-DCMAKE_PREFIX_PATH=${TFL_INSTALL_PREFIX}")

if(DEFINED TFL_CMAKE_GENERATOR AND NOT TFL_CMAKE_GENERATOR STREQUAL "")
    list(APPEND _truffle_consumer_configure -G "${TFL_CMAKE_GENERATOR}")
endif()

if(DEFINED TFL_CMAKE_MAKE_PROGRAM AND NOT TFL_CMAKE_MAKE_PROGRAM STREQUAL "")
    list(APPEND _truffle_consumer_configure "-DCMAKE_MAKE_PROGRAM=${TFL_CMAKE_MAKE_PROGRAM}")
endif()

if(DEFINED TFL_CXX_COMPILER AND NOT TFL_CXX_COMPILER STREQUAL "")
    list(APPEND _truffle_consumer_configure "-DCMAKE_CXX_COMPILER=${TFL_CXX_COMPILER}")
endif()

execute_process(
    COMMAND ${_truffle_consumer_configure}
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${TFL_CONSUMER_BUILD_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
