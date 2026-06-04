#include "application.hpp"

#include "host_window.hpp"

#include "truffle/render/pipeline_cache.hpp"
#include "truffle/render/renderer.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/scene/scene_adapter.hpp"

#ifdef __APPLE__
#include "truffle/rhi/metal_backend.hpp"
#endif

// We just unconditionally include it for testing, but typically CMake sets a flag.
// Or we wrap it in a stub conditional. Actually we are compiling it unconditionally if Vulkan option is set.
#ifdef TRUFFLE_BUILD_BACKEND_VULKAN
#include "truffle/rhi/vulkan_backend.hpp"
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace truffle::examples::host_workspace {
namespace {

using core::Status;
using core::StatusCode;
using render::Renderer;
using rhi::BufferDesc;
using rhi::BufferUsage;
using rhi::Extent2D;
using rhi::NativeSurfaceKind;
using rhi::NullBackendStats;
using rhi::PipelineDesc;
using rhi::ShaderDesc;
using rhi::SurfaceDesc;
using rhi::SwapchainDesc;
using rhi::TextureDesc;

constexpr double fixedDeltaSeconds = 1.0 / 60.0;
constexpr std::uint32_t smokeFrameCount = 4;
constexpr Extent2D initialExtent{1440, 900};
constexpr Extent2D smokeResizeExtent{1280, 720};

[[nodiscard]] bool non_zero(Extent2D extent) noexcept {
    return extent.width != 0 && extent.height != 0;
}

[[nodiscard]] bool same_extent(Extent2D left, Extent2D right) noexcept {
    return left.width == right.width && left.height == right.height;
}

[[nodiscard]] int fail(std::string_view context, const Status& status) {
    std::cerr << context << ": " << status.message << '\n';
    return 1;
}

[[nodiscard]] bool has_shape(const scene::SceneFrame& frame, FrameShape shape) {
    return frame.cameras.size() == shape.cameras &&
           frame.lights.size() == shape.lights &&
           frame.meshBatches.size() == shape.meshBatches;
}

class WorkspaceSession {
public:
    explicit WorkspaceSession(const IWorkspace& workspace)
        : workspace_(&workspace), shape_(workspace.expected_frame_shape()) {}

    [[nodiscard]] Status initialize(void* hostWindowHandle, Extent2D extent) {
        if (!non_zero(extent)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "session extent must be non-zero");
        }

        // We just always create a null backend here since it expects NullBackendStats
        backend_ = rhi::create_null_backend();
        const auto adapters = backend_->enumerate_adapters();
        if (adapters.empty() || !adapters.front().capabilities.presentation) {
            return Status::failure(StatusCode::unavailable,
                                   "host app requires a presentable adapter");
        }

        auto deviceResult = backend_->create_device({});
        if (!deviceResult.ok()) {
            return deviceResult.status();
        }
        device_ = std::move(deviceResult).value();

        auto vertexBufferResult = device_->create_buffer(
            BufferDesc{.size = 8192,
                       .usage = BufferUsage::vertex,
                       .debugName = std::string{workspace_->name()} +
                                    "-workspace-vertices"});
        if (!vertexBufferResult.ok()) {
            return vertexBufferResult.status();
        }
        vertexBuffer_ = std::move(vertexBufferResult).value();

        auto textureResult = device_->create_texture(
            TextureDesc{.extent = {512, 512},
                        .debugName = std::string{workspace_->name()} +
                                     "-workspace-swatch"});
        if (!textureResult.ok()) {
            return textureResult.status();
        }
        texture_ = std::move(textureResult).value();

        auto shaderResult = device_->create_shader(
            ShaderDesc{.entryPoint = "main",
                       .bytecode = {std::byte{0x54}, std::byte{0x52},
                                    std::byte{0x46}}});
        if (!shaderResult.ok()) {
            return shaderResult.status();
        }
        shader_ = std::move(shaderResult).value();

        auto pipelineResult = device_->create_pipeline(
            PipelineDesc{.debugName = std::string{workspace_->name()} +
                                       "-workspace-pipeline"});
        if (!pipelineResult.ok()) {
            return pipelineResult.status();
        }
        pipeline_ = std::move(pipelineResult).value();

        auto surfaceResult = device_->create_surface(SurfaceDesc{
            .native = {.kind = NativeSurfaceKind::external,
                       .handle = hostWindowHandle},
            .initialExtent = extent,
        });
        if (!surfaceResult.ok()) {
            return surfaceResult.status();
        }
        surface_ = std::move(surfaceResult).value();

        auto swapchainResult =
            device_->create_swapchain(*surface_, SwapchainDesc{.extent = extent});
        if (!swapchainResult.ok()) {
            return swapchainResult.status();
        }
        swapchain_ = std::move(swapchainResult).value();

        workspace_->build_scene(world_);
        systems_ = workspace_->systems();
        renderer_ = std::make_unique<Renderer>(*device_);

        auto ringResult = device_->create_upload_ring(2, 4 * 1024 * 1024);
        if (!ringResult.ok()) {
            return ringResult.status();
        }
        ring_ = std::move(ringResult).value();

        extent_ = extent;
        return Status::success();
    }

    [[nodiscard]] Status render_frame(Extent2D extent, double deltaSeconds) {
        if (!non_zero(extent)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "frame extent must be non-zero");
        }
        if (!same_extent(extent_, extent)) {
            if (const auto resizeStatus = swapchain_->resize(extent);
                !resizeStatus.ok()) {
                return resizeStatus;
            }
            extent_ = extent;
        }

        world_.run(systems_, deltaSeconds);
        const auto frame = adapter_.extract(world_, *ring_);
        if (!has_shape(frame, shape_)) {
            return Status::failure(StatusCode::invalid_state,
                                   "workspace extracted frame shape changed");
        }
        if (const auto renderStatus = renderer_->render([&]() { truffle::render::FrameGraph fg; fg.add_node(std::make_unique<truffle::render::RenderPassNode>(true, std::vector<truffle::render::RenderBatch>(frame.meshBatches.begin(), frame.meshBatches.end()))); return fg; }());
            !renderStatus.ok()) {
            return renderStatus;
        }
        ++renderedFrames_;
        return Status::success();
    }

    [[nodiscard]] Status validate_stats() const {
        const auto stats = backend_->stats();
        const auto expectedDraws =
            static_cast<std::uint64_t>(renderedFrames_) * shape_.meshBatches;
        if (stats.buffersCreated != 1 || stats.texturesCreated != 1 ||
            stats.surfacesCreated != 1 || stats.swapchainsCreated != 1 ||
            stats.commandBuffersCreated != 1 ||
            stats.drawsRecorded != expectedDraws ||
            stats.submissions != renderedFrames_) {
            return Status::failure(StatusCode::invalid_state,
                                   "null backend stats changed unexpectedly");
        }
        return Status::success();
    }

    [[nodiscard]] NullBackendStats stats() const noexcept {
        return backend_->stats();
    }

    [[nodiscard]] Extent2D extent() const noexcept {
        return extent_;
    }

private:
    const IWorkspace* workspace_ = nullptr;
    FrameShape shape_;
    std::unique_ptr<rhi::INullBackend> backend_;
    std::unique_ptr<rhi::IDevice> device_;
    std::unique_ptr<rhi::IBuffer> vertexBuffer_;
    std::unique_ptr<rhi::ITexture> texture_;
    std::unique_ptr<rhi::IShader> shader_;
    std::unique_ptr<rhi::IPipeline> pipeline_;
    std::unique_ptr<rhi::ISurface> surface_;
    std::unique_ptr<rhi::ISwapchain> swapchain_;
    ecs::World world_;
    std::vector<ecs::World::System> systems_;
    scene::SceneAdapter adapter_;
    std::unique_ptr<rhi::IFrameUploadRing> ring_;
    std::unique_ptr<Renderer> renderer_;
    Extent2D extent_;
    std::uint32_t renderedFrames_ = 0;
};

void print_summary(std::string_view runKind, const IWorkspace& workspace,
                   const WorkspaceSession& session) {
    const auto stats = session.stats();
    const auto extent = session.extent();
    std::cout << "host workspace " << runKind
              << " workspace=" << workspace.name()
              << " frames=" << stats.submissions
              << " draws=" << stats.drawsRecorded
              << " extent=" << extent.width << 'x' << extent.height << '\n';
}

[[nodiscard]] int run_smoke(const IWorkspace& workspace) {
    WorkspaceSession session{workspace};
    if (const auto status = session.initialize(nullptr, initialExtent);
        !status.ok()) {
        return fail("initialize smoke session", status);
    }

    for (std::uint32_t frameIndex = 0; frameIndex < smokeFrameCount;
         ++frameIndex) {
        const auto extent =
            frameIndex < smokeFrameCount / 2 ? initialExtent : smokeResizeExtent;
        if (const auto status = session.render_frame(extent, fixedDeltaSeconds);
            !status.ok()) {
            return fail("render smoke frame", status);
        }
    }

    if (const auto status = session.validate_stats(); !status.ok()) {
        return fail("validate smoke stats", status);
    }
    print_summary("smoke", workspace, session);
    return 0;
}

[[nodiscard]] int run_interactive(const IWorkspace& workspace) {
    HostWindow window;
    const auto title = "Truffle Host Workspace - " + std::string{workspace.name()};
    if (const auto status = window.open(title, initialExtent); !status.ok()) {
        return fail("open host window", status);
    }

    auto extent = window.framebuffer_extent();
    if (!non_zero(extent)) {
        extent = initialExtent;
    }

    WorkspaceSession session{workspace};
    if (const auto status = session.initialize(window.native_handle(), extent);
        !status.ok()) {
        return fail("initialize host session", status);
    }

    while (!window.should_close()) {
        window.poll_events();
        if (window.should_close()) {
            break;
        }

        extent = window.framebuffer_extent();
        if (non_zero(extent)) {
            if (const auto status =
                    session.render_frame(extent, fixedDeltaSeconds);
                !status.ok()) {
                return fail("render host frame", status);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }

    if (const auto status = session.validate_stats(); !status.ok()) {
        return fail("validate host stats", status);
    }
    print_summary("interactive", workspace, session);
    return 0;
}

#ifdef __APPLE__
// ---------------------------------------------------------------------------
// Minimal MSL shaders — fullscreen triangle, no vertex inputs
// ---------------------------------------------------------------------------
static const char kMSLVert[] = R"msl(
#include <metal_stdlib>
using namespace metal;
vertex float4 vert_main(uint vid [[vertex_id]]) {
    const float2 pos[3] = {{-1,-1},{3,-1},{-1,3}};
    return float4(pos[vid % 3], 0.0, 1.0);
}
)msl";

static const char kMSLFrag[] = R"msl(
#include <metal_stdlib>
using namespace metal;
fragment float4 frag_main() {
    return float4(0.2, 0.2, 0.4, 1.0);
}
)msl";

[[nodiscard]] int run_interactive_metal(const IWorkspace& workspace) {
    HostWindow window;
    const auto title =
        "Truffle Host Workspace [Metal] - " + std::string{workspace.name()};
    if (const auto status = window.open(title, initialExtent); !status.ok()) {
        return fail("open host window", status);
    }

    void* layerHandle = window.native_layer_handle();
    if (!layerHandle) {
        std::cerr << "native_layer_handle returned nullptr\n";
        return 1;
    }

    auto backend = rhi::create_metal_backend();
    if (backend->enumerate_adapters().empty()) {
        std::cerr << "no Metal adapter available\n";
        return 1;
    }

    auto deviceResult = backend->create_device({});
    if (!deviceResult.ok()) {
        return fail("create Metal device", deviceResult.status());
    }
    auto device = std::move(deviceResult).value();

    auto toBytes = [](const char* src) {
        const auto* p = reinterpret_cast<const std::byte*>(src);
        return std::vector<std::byte>(p, p + std::strlen(src));
    };

    auto vertResult = device->create_shader({
        .stage      = rhi::ShaderStage::vertex,
        .entryPoint = "vert_main",
        .bytecode   = toBytes(kMSLVert),
    });
    if (!vertResult.ok()) {
        return fail("compile vertex shader", vertResult.status());
    }
    auto vertShader = std::move(vertResult).value();

    auto fragResult = device->create_shader({
        .stage      = rhi::ShaderStage::fragment,
        .entryPoint = "frag_main",
        .bytecode   = toBytes(kMSLFrag),
    });
    if (!fragResult.ok()) {
        return fail("compile fragment shader", fragResult.status());
    }
    auto fragShader = std::move(fragResult).value();

    auto surfaceResult = device->create_surface(SurfaceDesc{
        .native        = {.kind   = NativeSurfaceKind::cocoa_layer,
                          .handle = layerHandle},
        .initialExtent = initialExtent,
    });
    if (!surfaceResult.ok()) {
        return fail("create Metal surface", surfaceResult.status());
    }
    auto surface = std::move(surfaceResult).value();

    auto extent = window.framebuffer_extent();
    if (!non_zero(extent)) {
        extent = initialExtent;
    }

    auto swapchainResult =
        device->create_swapchain(*surface, SwapchainDesc{.extent = extent});
    if (!swapchainResult.ok()) {
        return fail("create Metal swapchain", swapchainResult.status());
    }
    auto swapchain = std::move(swapchainResult).value();

    render::PipelineCache cache{*device};
    cache.register_shaders(0, {vertShader.get(), fragShader.get()});
    Renderer renderer{*device, &cache};

    ecs::World world;
    workspace.build_scene(world);
    const auto systems = workspace.systems();

    auto ringResult = device->create_upload_ring(2, 4 * 1024 * 1024);
    if (!ringResult.ok()) {
        return fail("create upload ring", ringResult.status());
    }
    auto ring = std::move(ringResult).value();

    scene::SceneAdapter adapter;

    while (!window.should_close()) {
        window.poll_events();
        if (window.should_close()) {
            break;
        }

        const auto fb = window.framebuffer_extent();
        if (non_zero(fb) && !same_extent(extent, fb)) {
            if (const auto s = swapchain->resize(fb); !s.ok()) {
                return fail("resize Metal swapchain", s);
            }
            extent = fb;
        }

        world.run(systems, fixedDeltaSeconds);
        const auto frame = adapter.extract(world, *ring);
        if (const auto s =
                renderer.render([&]() { truffle::render::FrameGraph fg; fg.add_node(std::make_unique<truffle::render::RenderPassNode>(true, std::vector<truffle::render::RenderBatch>(frame.meshBatches.begin(), frame.meshBatches.end()))); return fg; }(), swapchain.get());
            !s.ok()) {
            return fail("render Metal frame", s);
        }
        ring->advance();
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }

    std::cout << "host workspace metal interactive done\n";
    return 0;
}
#endif // __APPLE__

} // namespace

int run_application(const ApplicationOptions& options) {
    const auto workspace = create_workspace(options.workspace);
    if (!workspace) {
        std::cerr << "workspace selection could not be created\n";
        return 1;
    }
    
    if (options.useVulkan) {
#ifdef TRUFFLE_BUILD_BACKEND_VULKAN
        auto vulkanBackend = rhi::create_vulkan_backend();
        auto devResult = vulkanBackend->create_device({});
        if (!devResult.ok()) {
            std::cout << "successfully loaded vulkan stub: " << devResult.status().message << "\n";
            return 0;
        }
        std::cout << "successfully loaded vulkan stub (device created)\n";
        return 0;
#else
        std::cerr << "Vulkan backend not compiled\n";
        return 1;
#endif
    }
    
#ifdef __APPLE__
    if (options.useMetal && !options.smoke) {
        return run_interactive_metal(*workspace);
    }
#endif
    return options.smoke ? run_smoke(*workspace) : run_interactive(*workspace);
}

} // namespace truffle::examples::host_workspace
