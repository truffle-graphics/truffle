#include "application.hpp"

#include "host_window.hpp"

#include "truffle/render/renderer.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/scene/scene_adapter.hpp"

#ifdef __APPLE__
#include "truffle/rhi/metal_backend.hpp"
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

[[nodiscard]] int fail(std::string_view context, const core::Status& status) {
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

    [[nodiscard]] core::Status initialize(void* hostWindowHandle,
                                          rhi::Extent2D extent) {
        if (!non_zero(extent)) {
            return core::Status::failure(core::StatusCode::invalid_argument,
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
        renderer_ = std::make_unique<render::Renderer>(device_);
        extent_ = extent;
        return core::Status::success();
    }

    [[nodiscard]] core::Status render_frame(rhi::Extent2D extent,
                                            double deltaSeconds) {
        if (!non_zero(extent)) {
            return core::Status::failure(core::StatusCode::invalid_argument,
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
            return core::Status::failure(
                core::StatusCode::invalid_state,
                "workspace extracted frame shape changed");
        }
        render::FrameGraph graph;
        graph.add_node(std::make_unique<render::RenderPassNode>(
            true, std::vector<render::RenderBatch>(frame.meshBatches.begin(),
                                                   frame.meshBatches.end())));
        if (auto status = renderer_->render(graph, &swapchain_); !status.ok()) {
            return status;
        }
        if (auto status = ring_.advance(); !status.ok()) {
            return status;
        }
        ++renderedFrames_;
        return core::Status::success();
    }

    [[nodiscard]] core::Status validate_stats() const {
        const auto stats = instance_.stats();
        const auto expectedDraws =
            static_cast<std::uint64_t>(renderedFrames_) * shape_.meshBatches;
        if (stats.devicesCreated != 1 || stats.surfacesCreated != 1 ||
            stats.swapchainsCreated != 1 || stats.commandListsCreated == 0 ||
            stats.drawsRecorded != expectedDraws ||
            stats.submissions != renderedFrames_ ||
            stats.presentations != renderedFrames_) {
            return core::Status::failure(core::StatusCode::invalid_state,
                                         "null validation stats changed");
        }
        return core::Status::success();
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
    ecs::World world_;
    std::vector<ecs::World::System> systems_;
    scene::SceneAdapter sceneAdapter_;
    rhi::UploadRing ring_;
    std::unique_ptr<render::Renderer> renderer_;
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

#ifdef __APPLE__
[[nodiscard]] int run_metal_foundation() {
    auto instanceResult = rhi::create_metal_instance();
    if (!instanceResult.ok()) {
        return fail("create Metal instance", instanceResult.status());
    }
    auto instance = std::move(instanceResult).value();
    auto adapterResult = instance.adapter(0);
    if (!adapterResult.ok()) {
        return fail("enumerate Metal adapter", adapterResult.status());
    }
    auto adapter = std::move(adapterResult).value();
    auto deviceResult = adapter.request_device();
    if (!deviceResult.ok()) {
        return fail("create Metal device", deviceResult.status());
    }
    auto device = std::move(deviceResult).value();
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    if (!poolResult.ok()) {
        return fail("create Metal command pool", poolResult.status());
    }
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    if (!listResult.ok()) {
        return fail("allocate Metal command list", listResult.status());
    }
    auto list = std::move(listResult).value();
    if (auto status = list.begin(); !status.ok()) {
        return fail("begin Metal command list", status);
    }
    if (auto status = list.end(); !status.ok()) {
        return fail("end Metal command list", status);
    }
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    if (!queueResult.ok()) {
        return fail("get Metal queue", queueResult.status());
    }
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    if (auto status = queue.submit(lists); !status.ok()) {
        return fail("submit Metal command list", status);
    }
    std::cout << "Metal RHI 1 foundation submitted on " << adapter.info().name
              << '\n';
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
        if (result.ok() || result.status().code != core::StatusCode::unsupported) {
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
#ifdef __APPLE__
    if (options.useMetal && !options.smoke) {
        return run_metal_foundation();
    }
#endif
    return options.smoke ? run_smoke(*workspace) : run_interactive(*workspace);
}

} // namespace truffle::examples::host_workspace
