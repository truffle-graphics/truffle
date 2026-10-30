# No dependency acquisition is permitted here. Initialize this exact source
# closure explicitly; no binary SDK or implicit system fallback is used.
truffle_shader_require_pin(slang 90b8e77eafdfa0aabbbc48f0e86f6cfbde4cb8f8)
foreach(pair
        "miniz|6ef6c68f4fcbb8287aa8edf9c6670804932f41c6"
        "lz4|7f71be01f2c6f6b2c3cddb67eef8c38eab12ffd4"
        "unordered_dense|73f3cbb237e84d483afafc743f1f14ec53e12314"
        "lua|3fe7be956f23385aa1950dc31e2f25127ccfc0ea"
        "cmark|924936d0427cb25a61169739a7660230bffa6ea6"
        "fast_float|e0b53eaf63c6d00e0725788ef1dbb759aa321d79"
        "spirv-headers|c63848ecf2200425511319fd8bf2c17b751e501e"
        "vulkan|39f924b810e561fd86b2558b6711ca68d4363f68")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 dependency)
    list(GET parts 1 revision)
    truffle_shader_require_pin("slang/external/${dependency}" "${revision}")
endforeach()
foreach(feature CUDA OPTIX NVAPI XLIB AFTERMATH DX_ON_VK SLANG_RHI DXIL
        GFX SLANGD SLANGC SLANGI SLANGRT SLANG_GLSLANG TESTS EXAMPLES REPLAYER
        RELEASE_DEBUG_INFO RELEASE_LTO SLANG_PROXY PCH)
    set(SLANG_ENABLE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(dependency MINIZ LZ4 VULKAN_HEADERS SPIRV_HEADERS UNORDERED_DENSE)
    set(SLANG_USE_SYSTEM_${dependency} OFF CACHE BOOL "" FORCE)
    set(SLANG_OVERRIDE_${dependency}_PATH "" CACHE PATH "" FORCE)
endforeach()
foreach(dependency LUA CMARK FAST_FLOAT)
    set(SLANG_OVERRIDE_${dependency}_PATH "" CACHE PATH "" FORCE)
endforeach()
set(SLANG_SLANG_LLVM_FLAVOR DISABLE CACHE STRING "" FORCE)
set(SLANG_EXCLUDE_DAWN ON CACHE BOOL "" FORCE)
set(SLANG_EXCLUDE_TINT ON CACHE BOOL "" FORCE)
set(SLANG_WEBGPU_DAWN_BINARY_URL "" CACHE STRING "" FORCE)
set(SLANG_SLANG_TINT_BINARY_URL "" CACHE STRING "" FORCE)
set(SLANG_LIB_TYPE SHARED CACHE STRING "" FORCE)
set(SLANG_VERSION_FULL "2026.12.1" CACHE STRING "" FORCE)
set(SLANG_VERSION_NUMERIC "2026.12.1" CACHE STRING "" FORCE)
set(SLANG_EMBED_CORE_MODULE ON CACHE BOOL "" FORCE)
set(SLANG_EMBED_CORE_MODULE_SOURCE ON CACHE BOOL "" FORCE)
set(SLANG_STANDARD_MODULE_DEVELOP_BUILD OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/vendor/slang"
                 "${PROJECT_BINARY_DIR}/dependencies/slang" EXCLUDE_FROM_ALL)
target_link_libraries(truffle_shader PRIVATE slang)
target_compile_definitions(truffle_shader PRIVATE TRUFFLE_SLANG_PROVIDER="bundled-source")
if(TRUFFLE_INSTALL)
    # Export only the compiler runtime used by Shader, not the upstream SDK,
    # optional plugins, generator executables or unrelated targets.
    set_target_properties(slang PROPERTIES EXPORT_NAME SlangCompiler PUBLIC_HEADER "")
    install(TARGETS slang EXPORT TruffleTargets
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${PROJECT_SOURCE_DIR}/vendor/slang/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/slang")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/vendor/slang/LICENSES/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/slang/LICENSES")
    foreach(notice miniz/LICENSE lz4/lib/LICENSE unordered_dense/LICENSE lua/lua.h
            spirv-headers/LICENSE vulkan/LICENSE.md
            cmark/COPYING fast_float/LICENSE-MIT fast_float/LICENSE-APACHE fast_float/LICENSE-BOOST)
        get_filename_component(dependency "${notice}" DIRECTORY)
        install(FILES "${PROJECT_SOURCE_DIR}/vendor/slang/external/${notice}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/slang/${dependency}")
    endforeach()
    foreach(dependency spirv-headers vulkan)
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/vendor/slang/external/${dependency}/LICENSES/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/Truffle/slang/${dependency}/LICENSES")
    endforeach()
endif()
target_sources(truffle_shader PRIVATE slang_adapter.cpp)
target_compile_definitions(truffle_shader PRIVATE
    TRUFFLE_SHADER_HAS_SLANG=1
    TRUFFLE_SLANG_REVISION="90b8e77eafdfa0aabbbc48f0e86f6cfbde4cb8f8")
