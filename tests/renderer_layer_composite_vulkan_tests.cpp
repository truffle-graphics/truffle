#include "renderer_layer_composite.hpp"
#include "renderer_native_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/source_program.hpp"
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"

namespace {

using namespace truffle;

// Render-to-texture proof (#215) under the Vulkan validation layer, where the
// pass transitions are observable.
constexpr const char* kFullscreen = R"(#version 450
void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kWriter = R"(#version 450
layout(location=0) out vec4 color;
void main() { color = vec4(1.0, 0.0, 0.0, 1.0); }
)";

constexpr const char* kCompositor = R"(#version 450
layout(set=0, binding=0) uniform texture2D layer;
layout(set=0, binding=1) uniform sampler layerSampler;
layout(location=0) out vec4 color;
void main() { color = texture(sampler2D(layer, layerSampler), gl_FragCoord.xy / 16.0); }
)";

renderer::SourceProgramRequest request(const char* fragment) {
    renderer::SourceProgramRequest result;
    result.vertex = {.name = "fullscreen", .source = {"fullscreen.vert", kFullscreen},
                     .stage = rhi::ShaderStage::vertex};
    result.fragment = {.name = "layer", .source = {"layer.frag", fragment},
                       .stage = rhi::ShaderStage::fragment};
    return result;
}

int run() {
    auto created = rhi::create_vulkan_instance({.enableValidation = true});
    if (!created.ok()) {
        std::cout << "Vulkan runtime unavailable: " << created.status().message << '\n';
        return 77;
    }
    auto instance = std::move(created).value();
    if (rhi::detail::last_vulkan_validation_state_for_testing() ==
        rhi::detail::VulkanValidationState::failed_to_load) {
        std::cerr << "VK_LAYER_KHRONOS_validation failed to load\n";
        return 1;
    }
    auto adapter = tests::require(instance.adapter(0));
    auto device = tests::require(adapter.request_device());

    auto bindings = tests::make_layer_bindings(device);
    renderer::PipelineCache programs(device);
    renderer::Renderer renderer(device, &programs);
    shader::Compiler compiler;
    rhi::PipelineDesc writerState;
    writerState.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
    writerState.dynamicState = rhi::DynamicState::scissor;
    renderer::SourceProgram writer(programs, 1, writerState);
    tests::require(writer.rebuild(compiler, request(kWriter)));
    rhi::PipelineDesc compositorState;
    compositorState.layout = &bindings.pipelineLayout;
    compositorState.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
    renderer::SourceProgram compositor(programs, 2, compositorState);
    tests::require(compositor.rebuild(compiler, request(kCompositor)));
    tests::LayerComposite layer(device, renderer, bindings, 1, 2);
    layer.run_proof();
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "Vulkan render-to-texture proof failed: " << error.what() << '\n';
        return 1;
    }
}
