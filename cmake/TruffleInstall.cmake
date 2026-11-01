include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(TruffleWarnings)

function(truffle_configure_target target export_name)
    target_include_directories(
        ${target}
        PUBLIC
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_compile_features(${target} PUBLIC cxx_std_20)
    truffle_target_warnings(${target})
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${export_name})

    if(TFL_INSTALL)
        install(
            TARGETS ${target}
            EXPORT TruffleTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endfunction()

function(truffle_install_package)
    install(
        DIRECTORY "${PROJECT_SOURCE_DIR}/include/truffle"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.hpp")

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/TruffleConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/TruffleConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Truffle")
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/TruffleConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(
        EXPORT TruffleTargets
        FILE TruffleTargets.cmake
        NAMESPACE Truffle::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Truffle")
    install(
        FILES
            "${PROJECT_BINARY_DIR}/TruffleConfig.cmake"
            "${PROJECT_BINARY_DIR}/TruffleConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Truffle")
endfunction()
