function(truffle_add_format_targets)
    file(
        GLOB_RECURSE truffle_format_files
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/*.hpp"
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/tests/*.cpp"
        "${PROJECT_SOURCE_DIR}/tests/*.hpp"
        "${PROJECT_SOURCE_DIR}/examples/host_workspace/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/examples/host_workspace/src/*.hpp")

    find_program(TFL_CLANG_FORMAT clang-format)
    if(NOT TFL_CLANG_FORMAT)
        add_custom_target(
            truffle_format
            COMMAND "${CMAKE_COMMAND}" -E echo
                "clang-format was not found. Install it to format Truffle sources."
            VERBATIM)
        add_custom_target(
            truffle_format_check
            COMMAND "${CMAKE_COMMAND}" -E echo
                "clang-format was not found. Install it to check Truffle formatting."
            VERBATIM)
        return()
    endif()

    add_custom_target(
        truffle_format
        COMMAND "${TFL_CLANG_FORMAT}" -i ${truffle_format_files}
        COMMAND_EXPAND_LISTS
        VERBATIM)
    add_custom_target(
        truffle_format_check
        COMMAND "${TFL_CLANG_FORMAT}" --dry-run --Werror ${truffle_format_files}
        COMMAND_EXPAND_LISTS
        VERBATIM)
endfunction()
