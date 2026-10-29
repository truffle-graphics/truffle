#include "application.hpp"

#include "host_window.hpp"

#include "truffle/renderer/renderer.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/encom_renderer/encom_render_adapter.hpp"

#ifdef TRUFFLE_HOST_WORKSPACE_METAL
#include "metal_host_presenter.hpp"
#endif

#ifdef TRUFFLE_BUILD_BACKEND_VULKAN
#include "truffle/rhi/vulkan_backend.hpp"
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace truffle::examples::host_workspace {
namespace {

constexpr double kFixedDeltaSeconds = 1.0 / 60.0;
constexpr std::uint32_t kSmokeFrameCount = 4;
constexpr rhi::Extent2D kInitialExtent{1440, 900};
constexpr rhi::Extent2D kSmokeResizeExtent{1280, 720};

[[nodiscard]] bool non_zero(rhi::Extent2D extent) noexcept {
    return extent.width != 0 && extent.height != 0;
}

[[nodiscard]] bool same_extent(rhi::Extent2D lhs, rhi::Extent2D rhs) noexcept {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

[[nodiscard]] int fail(std::string_view context, const base::Status& status) {
    std::cerr << context << ": " << status.message << '\n';
    return 1;
}

[[nodiscard]] bool has_shape(const encom_renderer::EnComRenderFrame& frame, FrameShape shape) {
    return frame.cameras.size() == shape.cameras &&
           frame.lights.size() == shape.lights &&
           frame.meshBatches.size() == shape.meshBatches;
}

class WorkspaceSession {
public:
    explicit WorkspaceSession(const IWorkspace& workspace)
        : workspace_(&workspace), shape_(workspace.expected_frame_shape()) {}

    [[nodiscard]] base::Status initialize(void* hostWindowHandle,
                                          rhi::Extent2D extent) {
        if (!non_zero(extent)) {
            return base::Status::failure(base::StatusCode::invalid_argument,
                                         "session extent must be non-zero");
        }
        auto instanceResult = rhi::create_null_instance();
        if (!instanceResult.ok()) {
            return instanceResult.status();
        }
        instance_ = std::move(instanceResult).value();
        auto adapterResult = instance_.adapter(0);
        if (!adapterResult.ok()) {
            return adapterResult.status();
        }
        adapter_ = std::move(adapterResult).value();
        auto deviceResult = adapter_.request_device();
        if (!deviceResult.ok()) {
            return deviceResult.status();
        }
        device_ = std::move(deviceResult).value();

        const auto native = hostWindowHandle != nullptr
                                ? rhi::NativeSurface{
                                      .kind = rhi::NativeSurfaceKind::external,
                                      .handle = hostWindowHandle,
                                  }
                                : rhi::NativeSurface{};
        auto surfaceResult = device_.create_surface({
            .native = native,
            .initialExtent = extent,
        });
        if (!surfaceResult.ok()) {
            return surfaceResult.status();
        }
        surface_ = std::move(surfaceResult).value();
        auto swapchainResult = device_.create_swapchain(
            surface_, rhi::SwapchainDesc{.extent = extent});
        if (!swapchainResult.ok()) {
            return swapchainResult.status();
        }
        swapchain_ = std::move(swapchainResult).value();
        auto ringResult = device_.create_upload_ring(2, 4 * 1024 * 1024);
        if (!ringResult.ok()) {
            return ringResult.status();
        }
        ring_ = std::move(ringResult).value();

        workspace_->build_scene(world_);
        systems_ = workspace_->systems();
        renderer_ = std::make_unique<renderer::Renderer>(device_);
        extent_ = extent;
        return base::Status::success();
    }

    [[nodiscard]] base::Status render_frame(rhi::Extent2D extent,
                                            double deltaSeconds) {
        if (!non_zero(extent)) {
            return base::Status::failure(base::StatusCode::invalid_argument,
                                         "frame extent must be non-zero");
        }
        if (!same_extent(extent_, extent)) {
            if (auto status = swapchain_.resize(extent); !status.ok()) {
                return status;
            }
            extent_ = extent;
        }
        world_.run(systems_, deltaSeconds);
        const auto frame = sceneAdapter_.extract(world_, ring_);
        if (!has_shape(frame, shape_)) {
            return base::Status::failure(
                base::StatusCode::invalid_state,
                "workspace extracted frame shape changed");
        }
        renderer::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::RenderPassNode>(
            true, std::vector<renderer::RenderBatch>(frame.meshBatches.begin(),
                                                   frame.meshBatches.end())));
        if (auto status = renderer_->render(graph, &swapchain_); !status.ok()) {
            return status;
        }
        if (auto status = ring_.advance(); !status.ok()) {
            return status;
        }
        ++renderedFrames_;
        return base::Status::success();
    }

    [[nodiscard]] base::Status validate_stats() const {
        const auto stats = instance_.stats();
        const auto expectedDraws =
            static_cast<std::uint64_t>(renderedFrames_) * shape_.meshBatches;
        if (stats.devicesCreated != 1 || stats.surfacesCreated != 1 ||
            stats.swapchainsCreated != 1 || stats.commandListsCreated == 0 ||
            stats.drawsRecorded != expectedDraws ||
            stats.submissions != renderedFrames_ ||
            stats.presentations != renderedFrames_) {
            return base::Status::failure(base::StatusCode::invalid_state,
                                         "null validation stats changed");
        }
        return base::Status::success();
    }

    [[nodiscard]] rhi::BackendStats stats() const noexcept {
        return instance_.stats();
    }

    [[nodiscard]] rhi::Extent2D extent() const noexcept { return extent_; }

private:
    const IWorkspace* workspace_ = nullptr;
    FrameShape shape_;
    rhi::Instance instance_;
    rhi::Adapter adapter_;
    rhi::Device device_;
    rhi::Surface surface_;
    rhi::Swapchain swapchain_;
    encom::World world_;
    std::vector<encom::World::System> systems_;
    encom_renderer::EnComRenderAdapter sceneAdapter_;
    rhi::UploadRing ring_;
    std::unique_ptr<renderer::Renderer> renderer_;
    rhi::Extent2D extent_;
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
    if (auto status = session.initialize(nullptr, kInitialExtent); !status.ok()) {
        return fail("initialize smoke session", status);
    }
    for (std::uint32_t frame = 0; frame < kSmokeFrameCount; ++frame) {
        const auto extent = frame < kSmokeFrameCount / 2 ? kInitialExtent
                                                         : kSmokeResizeExtent;
        if (auto status = session.render_frame(extent, kFixedDeltaSeconds);
            !status.ok()) {
            return fail("render smoke frame", status);
        }
    }
    if (auto status = session.validate_stats(); !status.ok()) {
        return fail("validate smoke stats", status);
    }
    print_summary("smoke", workspace, session);
    return 0;
}

[[nodiscard]] int run_interactive(const IWorkspace& workspace) {
    HostWindow window;
    const auto title = "Truffle Host Workspace - " +
                       std::string{workspace.name()};
    if (auto status = window.open(title, kInitialExtent); !status.ok()) {
        return fail("open host window", status);
    }
    auto extent = window.framebuffer_extent();
    if (!non_zero(extent)) {
        extent = kInitialExtent;
    }
    WorkspaceSession session{workspace};
    if (auto status = session.initialize(window.native_handle(), extent);
        !status.ok()) {
        return fail("initialize host session", status);
    }
    while (!window.should_close()) {
        window.poll_events();
        extent = window.framebuffer_extent();
        if (non_zero(extent)) {
            if (auto status = session.render_frame(extent, kFixedDeltaSeconds);
                !status.ok()) {
                return fail("render host frame", status);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }
    if (auto status = session.validate_stats(); !status.ok()) {
        return fail("validate host stats", status);
    }
    print_summary("interactive", workspace, session);
    return 0;
}

#ifdef TRUFFLE_HOST_WORKSPACE_METAL
constexpr rhi::Extent2D kMetalWindowExtent{640, 400};
constexpr rhi::Extent2D kMetalResizedWindowExtent{800, 520};
constexpr std::uint32_t kResizeSettleAttempts = 60;

[[nodiscard]] rhi::ClearColor frame_clear_color(std::uint32_t frame) noexcept {
    constexpr std::array<rhi::ClearColor, 3> colors{{
        {0.10F, 0.20F, 0.40F, 1.0F},
        {0.15F, 0.35F, 0.20F, 1.0F},
        {0.40F, 0.15F, 0.20F, 1.0F},
    }};
    return colors[(frame / 30) % colors.size()];
}

// The host owns the GLFW window and its CAMetalLayer; Truffle receives only
// the layer as a cocoa_layer surface. A bounded run scripts one window resize
// halfway through and requires the swapchain to track the new framebuffer.
[[nodiscard]] int run_metal_host_window(std::uint32_t boundedFrames) {
    HostWindow window;
    if (auto status = window.open("Truffle Host Workspace - Metal",
                                  kMetalWindowExtent);
        !status.ok()) {
        return fail("open host window", status);
    }
    void* layer = window.native_layer_handle();
    const auto initialExtent = window.framebuffer_extent();
    MetalHostPresenter presenter;
    if (auto status = presenter.initialize(layer, initialExtent); !status.ok()) {
        return fail("initialize Metal host presenter", status);
    }
    std::uint32_t frame = 0;
    while (!window.should_close() &&
           (boundedFrames == 0 || frame < boundedFrames)) {
        if (boundedFrames != 0 && frame == boundedFrames / 2) {
            window.set_size(kMetalResizedWindowExtent);
            for (std::uint32_t attempt = 0;
                 attempt < kResizeSettleAttempts &&
                 same_extent(window.framebuffer_extent(), initialExtent);
                 ++attempt) {
                window.poll_events();
                std::this_thread::sleep_for(std::chrono::milliseconds{16});
            }
        }
        window.poll_events();
        const auto extent = window.framebuffer_extent();
        if (!non_zero(extent)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{16});
            continue;
        }
        if (auto status =
                presenter.present_frame(extent, frame_clear_color(frame));
            !status.ok()) {
            return fail("present Metal host frame", status);
        }
        ++frame;
    }
    const auto& stats = presenter.stats();
    const auto finalExtent = presenter.extent();
    std::cout << "Metal host window adapter=" << presenter.adapter_name()
              << " frames=" << stats.framesPresented
              << " resizes=" << stats.swapchainResizes
              << " suboptimal=" << stats.suboptimalAcquires
              << " out_of_date=" << stats.outOfDateAcquires
              << " extent=" << initialExtent.width << 'x'
              << initialExtent.height << "->" << finalExtent.width << 'x'
              << finalExtent.height << '\n';
    if (boundedFrames != 0 &&
        (stats.framesPresented != boundedFrames ||
         stats.swapchainResizes == 0 ||
         same_extent(finalExtent, initialExtent) ||
         !same_extent(finalExtent, window.framebuffer_extent()))) {
        std::cerr << "Metal host window run did not present every frame "
                     "across a tracked resize\n";
        return 1;
    }
    return 0;
}
#endif

} // namespace

int run_application(const ApplicationOptions& options) {
    const auto workspace = create_workspace(options.workspace);
    if (!workspace) {
        std::cerr << "workspace selection could not be created\n";
        return 1;
    }
    if (options.useVulkan) {
#ifdef TRUFFLE_BUILD_BACKEND_VULKAN
        const auto result = rhi::create_vulkan_instance();
        if (result.ok() || result.status().code != base::StatusCode::unsupported) {
            std::cerr << "Vulkan RHI 1 availability contract changed\n";
            return 1;
        }
        std::cout << result.status().message << '\n';
        return 0;
#else
        std::cerr << "Vulkan backend target is not compiled\n";
        return 1;
#endif
    }
    if (options.useMetal && !options.smoke) {
#ifdef TRUFFLE_HOST_WORKSPACE_METAL
        return run_metal_host_window(options.frames);
#else
        std::cerr << "Metal backend target is not compiled\n";
        return 1;
#endif
    }
    return options.smoke ? run_smoke(*workspace) : run_interactive(*workspace);
}

} // namespace truffle::examples::host_workspace
