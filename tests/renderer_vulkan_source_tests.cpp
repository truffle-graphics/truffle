#include "renderer_native_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/source_program.hpp"
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"

#include <filesystem>
#include <fstream>

namespace {

using namespace truffle;

const std::filesystem::path root{TRUFFLE_RENDERER_SOURCE_DIR};

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (!output.good()) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

// The host owns polling: re-read the root sources and hand Renderer a request.
renderer::SourceProgramRequest request(bool extraInput = false) {
    renderer::SourceProgramRequest result;
    result.vertex = {.name = "fullscreen",
                     .source = tests::require(shader::read_source(
                         (root / "fullscreen.vert").string())),
                     .stage = rhi::ShaderStage::vertex};
    result.fragment = {.name = "palette",
                       .source = tests::require(shader::read_source(
                           (root / "palette.frag").string())),
                       .stage = rhi::ShaderStage::fragment,
                       .includeRoots = {(root / "include").string()},
                       .defines = {{"PALETTE_ENTRY", "0"}}};
    if (extraInput) {
        result.fragment.defines.push_back({"EXTRA_INPUT", "1"});
    }
    return result;
}

std::string palette(const char* first) {
    return std::string("#ifndef PALETTE_GLSL\n#define PALETTE_GLSL\n"
                       "#include \"channels.glsl\"\n"
                       "vec4 palette(int entry) {\n"
                       "    return entry == 0 ? ") +
           first + " : vec4(1.0);\n}\n#endif\n";
}

int run() {
    auto created = rhi::create_vulkan_instance({.enableValidation = true});
    if (!created.ok()) {
        std::cout << "Vulkan runtime unavailable: " << created.status().message << '\n';
        return 77;
    }
    auto instance = std::move(created).value();
    const auto validation = rhi::detail::last_vulkan_validation_state_for_testing();
    if (validation == rhi::detail::VulkanValidationState::failed_to_load) {
        std::cerr << "VK_LAYER_KHRONOS_validation failed to load\n";
        return 1;
    }
    std::cout << "Vulkan validation layer "
              << (validation == rhi::detail::VulkanValidationState::active ? "active"
                                                                            : "not installed")
              << '\n';
    auto adapter = tests::require(instance.adapter(0));
    TRUFFLE_CHECK(adapter.info().native);
    std::cout << "adapter: " << adapter.info().name << '\n';
    auto device = tests::require(adapter.request_device());

    write(root / "fullscreen.vert", R"(#version 450
layout(location=0) flat out vec4 tint;
void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
    tint = vec4(1.0);
}
)");
    write(root / "palette.frag", R"(#version 450
#include "palette.glsl"
layout(location=0) flat in vec4 tint;
#ifdef EXTRA_INPUT
layout(location=1) flat in vec4 extra;
#endif
layout(location=0) out vec4 color;
void main() { color = palette(PALETTE_ENTRY) * tint; }
)");
    write(root / "include" / "channels.glsl",
          "#define OPAQUE(r, g, b) vec4(r, g, b, 1.0)\n");
    write(root / "include" / "palette.glsl", palette("OPAQUE(1.0, 0.0, 0.0)"));

    constexpr renderer::MaterialId material = 3;
    constexpr std::array red{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    constexpr std::array green{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    constexpr std::array blue{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
    renderer::PipelineCache programs(device);
    renderer::Renderer renderer(device, &programs);
    rhi::PipelineDesc state;
    state.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
    renderer::SourceProgram program(programs, material, std::move(state));
    shader::Compiler compiler;
    auto target = tests::make_color_target(device);
    renderer::RenderBatch batch;
    batch.material = material;
    batch.instanceCount = 1;
    const auto render = [&](const auto& color, const char* label) {
        tests::require(renderer.render(tests::color_graph(target, batch)));
        tests::check_color(device, target, color, label);
    };

    auto first = tests::require(program.rebuild(compiler, request()));
    TRUFFLE_CHECK(first.published && !first.vertexCacheHit && !first.fragmentCacheHit);
    const auto& compiled = *program.published_fragment();
    TRUFFLE_CHECK(compiled.package.desc().variants.size() == 1);
    TRUFFLE_CHECK(compiled.package.desc().variants.front().target == rhi::ShaderTarget::spirv);
    TRUFFLE_CHECK(compiled.package.desc().permutations.size() == 1);
    TRUFFLE_CHECK(compiled.dependencies.size() == 3);
    std::cout << "runtime GLSL->SPIR-V fragment: " << compiled.dependencies.size()
              << " source files, 1 requested configuration\n";
    render(red, "runtime-compiled initial program");
    const auto initialId = programs.published(material)->id();
    const auto pipelineCount = instance.stats().pipelinesCreated;

    auto unchanged = tests::require(program.rebuild(compiler, request()));
    TRUFFLE_CHECK(!unchanged.published && unchanged.vertexCacheHit &&
                  unchanged.fragmentCacheHit);
    TRUFFLE_CHECK(instance.stats().pipelinesCreated == pipelineCount);
    TRUFFLE_CHECK(programs.published(material)->id() == initialId);
    render(red, "unchanged sources hit the compile cache and keep the program");

    // Record the published program, then replace it before submission: RHI keeps
    // the old native payload alive for the recorded work.
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto encoder = tests::require(list.begin_rendering({
        .extent = {16, 16}, .colorAttachments = {{.texture = &target}},
    }));
    tests::require(encoder.bind_pipeline(
        *tests::require(programs.get_or_create({}, material))));
    tests::require(encoder.draw(3, 1));
    tests::require(encoder.end());
    tests::require(list.end());

    write(root / "include" / "palette.glsl", palette("OPAQUE(0.0, 0.0, 1.0)"));
    auto changed = tests::require(program.rebuild(compiler, request()));
    TRUFFLE_CHECK(changed.published && changed.vertexCacheHit && !changed.fragmentCacheHit);
    TRUFFLE_CHECK(programs.published(material)->id() != initialId);
    tests::submit_graphics(device, list);
    tests::check_color(device, target, red, "recorded old program survives rebuild adoption");
    render(blue, "include dependency change adopted");
    const auto blueId = programs.published(material)->id();
    const auto blueKey = program.published_fragment()->cacheKey;

    const auto retained = [&](const base::Status& status, const char* label) {
        if (status.ok() || status.message.empty() ||
            programs.published(material)->id() != blueId ||
            program.published_fragment()->cacheKey != blueKey) {
            throw std::runtime_error(std::string(label) + ": previous program not retained");
        }
        std::cout << label << " diagnostic: " << status.message.substr(0, 160) << '\n';
        render(blue, label);
    };
    write(root / "include" / "palette.glsl", "vec4 palette(int entry) { return undefined_value; }\n");
    const auto compileFailure = program.rebuild(compiler, request()).status();
    TRUFFLE_CHECK(compileFailure.message.find("undefined_value") != std::string::npos);
    retained(compileFailure, "failed include rebuild keeps previous output");
    write(root / "include" / "palette.glsl", palette("OPAQUE(0.0, 0.0, 1.0)"));
    const auto interfaceFailure = program.rebuild(compiler, request(true)).status();
    TRUFFLE_CHECK(interfaceFailure.code == base::StatusCode::invalid_argument);
    retained(interfaceFailure, "stage interface mismatch keeps previous output");
    auto unboundRequest = request();
    unboundRequest.fragment.source.text =
        "#version 450\n#include \"palette.glsl\"\n"
        "layout(location=0) flat in vec4 tint;\n"
        "layout(set=0,binding=0) uniform Tint { vec4 value; } extra;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color = palette(PALETTE_ENTRY) * tint * extra.value; }\n";
    retained(program.rebuild(compiler, unboundRequest).status(),
             "binding/layout validation failure keeps previous output");

    write(root / "include" / "palette.glsl", palette("OPAQUE(0.0, 1.0, 0.0)"));
    auto recovered = tests::require(program.rebuild(compiler, request()));
    TRUFFLE_CHECK(recovered.published && !recovered.fragmentCacheHit);
    render(green, "recovered rebuild adopted");
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "Vulkan Renderer source-program proof failed: " << error.what() << '\n';
        return 1;
    }
}
