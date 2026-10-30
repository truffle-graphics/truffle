set(_config)
if(TRUFFLE_CONFIG)
    list(APPEND _config --config "${TRUFFLE_CONFIG}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${TRUFFLE_BINARY_DIR}"
    --prefix "${TRUFFLE_INSTALL_PREFIX}" ${_config} COMMAND_ERROR_IS_FATAL ANY)
cmake_path(ABSOLUTE_PATH TRUFFLE_INSTALL_LIBDIR
    BASE_DIRECTORY "${TRUFFLE_INSTALL_PREFIX}" NORMALIZE OUTPUT_VARIABLE _install_libdir)
if(TRUFFLE_SHADER_ENABLE_GLSLANG)
    cmake_path(ABSOLUTE_PATH TRUFFLE_INSTALL_DATADIR
        BASE_DIRECTORY "${TRUFFLE_INSTALL_PREFIX}" NORMALIZE OUTPUT_VARIABLE _install_datadir)
    set(_glslang_license "${_install_datadir}/licenses/Truffle/glslang/LICENSE.txt")
    if(NOT EXISTS "${_glslang_license}")
        message(FATAL_ERROR "Installed glslang license notice is missing: ${_glslang_license}")
    endif()
    file(SHA256 "${TRUFFLE_SOURCE_DIR}/vendor/glslang/LICENSE.txt" _source_license_hash)
    file(SHA256 "${_glslang_license}" _installed_license_hash)
    if(NOT _source_license_hash STREQUAL _installed_license_hash)
        message(FATAL_ERROR "Installed glslang license notice differs from the pinned source")
    endif()
endif()
if(TRUFFLE_BUILD_SHADERC)
    cmake_path(ABSOLUTE_PATH TRUFFLE_INSTALL_BINDIR
        BASE_DIRECTORY "${TRUFFLE_INSTALL_PREFIX}" NORMALIZE OUTPUT_VARIABLE _install_bindir)
    set(_shaderc "${_install_bindir}/truffle-shaderc${TRUFFLE_EXECUTABLE_SUFFIX}")
    message(STATUS "Checking installed shaderc: ${_shaderc}")
    set(_shaderc_command "${CMAKE_COMMAND}" -E env
        --unset=DYLD_LIBRARY_PATH --unset=DYLD_FALLBACK_LIBRARY_PATH --unset=LD_LIBRARY_PATH
        "${_shaderc}")
    execute_process(COMMAND ${_shaderc_command} --self-test COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${_shaderc_command} --routes COMMAND_ERROR_IS_FATAL ANY)
else()
    message(STATUS "Skipping installed shaderc execution: TRUFFLE_BUILD_SHADERC=OFF")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${TRUFFLE_SOURCE_DIR}/tests/shader_consumer"
    -B "${TRUFFLE_CONSUMER_BUILD_DIR}"
    -G "${TRUFFLE_CMAKE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${_install_libdir};${TRUFFLE_INSTALL_PREFIX}"
    "-DTruffle_DIR=${_install_libdir}/cmake/Truffle"
    "-DCMAKE_OSX_SYSROOT=${TRUFFLE_OSX_SYSROOT}"
    "-DCMAKE_CXX_COMPILER=${TRUFFLE_CXX_COMPILER}"
    "-DEXPECT_COMPILER=${TRUFFLE_EXPECT_COMPILER}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TRUFFLE_CONSUMER_BUILD_DIR}"
    --parallel 2 ${_config} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${TRUFFLE_CONSUMER_BUILD_DIR}/shader_consumer${TRUFFLE_EXECUTABLE_SUFFIX}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${TRUFFLE_CONSUMER_BUILD_DIR}/prepared_consumer${TRUFFLE_EXECUTABLE_SUFFIX}"
    COMMAND_ERROR_IS_FATAL ANY)
