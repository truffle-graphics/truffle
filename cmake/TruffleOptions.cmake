option(TFL_BUILD_TESTS "Build Truffle contract tests" ${PROJECT_IS_TOP_LEVEL})
option(TFL_BUILD_EXAMPLES "Build Truffle examples" ${PROJECT_IS_TOP_LEVEL})
option(TFL_INSTALL "Add Truffle install and package export rules" ${PROJECT_IS_TOP_LEVEL})
option(TFL_WARNINGS_AS_ERRORS "Treat Truffle target warnings as errors" OFF)
option(TFL_BUILD_SHADERC "Build the optional truffle-shaderc package tool" OFF)
option(TFL_SHADER_ENABLE_GLSLANG
       "Enable the shared GLSL/GLSL ES to SPIR-V compiler adapter" OFF)
option(TFL_SHADER_ENABLE_SLANG
       "Enable native Slang modules to validated SPIR-V" OFF)
option(TFL_SHADER_ENABLE_TINT
       "Enable pinned in-process Tint WGSL compilation and validation" OFF)
option(TFL_SHADER_ENABLE_SPIRV_TOOLS
       "Validate emitted SPIR-V with pinned SPIRV-Tools" OFF)
option(TFL_SHADER_ENABLE_SPIRV_CROSS
       "Translate validated portable SPIR-V to Metal/GLSL/GLSL ES" OFF)
option(TFL_SHADER_ENABLE_DXC
       "Enable the optional pinned DXC HLSL 2021 compiler adapter" OFF)
option(TFL_SHADER_ENABLE_METAL_COMPILER
       "Enable the optional in-process native MSL compiler/reflection adapter (Apple only)" OFF)
option(TFL_SHADER_ENABLE_METALLIB_COMPILER
       "Enable optional offline MSL-to-metallib compilation (Apple Metal Toolchain required)" OFF)
option(TFL_GUI_ENABLE_TEXT
       "Enable GUI text shaping and rasterization (FreeType and HarfBuzz)" ON)
option(TFL_BUILD_RHI_DOCTOR "Build the RHI backend/platform doctor tool" ${PROJECT_IS_TOP_LEVEL})

option(TFL_BUILD_BACKEND_NULL    "Build the Truffle null validation backend" ON)
option(TFL_BUILD_BACKEND_VULKAN  "Build the Vulkan RHI 1 target (currently unavailable)" OFF)
option(TFL_BUILD_BACKEND_DIRECT3D "Build the Direct3D 12 RHI 1 target (currently unavailable)" OFF)
if(APPLE)
    option(TFL_BUILD_BACKEND_METAL "Build the Truffle Metal backend" ${PROJECT_IS_TOP_LEVEL})
else()
    option(TFL_BUILD_BACKEND_METAL "Build the Truffle Metal backend" OFF)
endif()
option(TFL_BUILD_BACKEND_OPENGL  "Build the OpenGL RHI 1 target (currently unavailable)" OFF)
option(TFL_BUILD_BACKEND_OPENGLES "Build the OpenGL ES RHI 1 target" OFF)
option(TFL_BUILD_BACKEND_WEBGPU  "Build the WebGPU RHI 1 target" OFF)
option(TFL_BUILD_BACKEND_WEBGL2  "Build the WebGL2 RHI 1 target" OFF)
