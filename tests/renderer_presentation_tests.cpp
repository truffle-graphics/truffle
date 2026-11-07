#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/specialist/presentation.hpp"

#if defined(TFL_TEST_PRESENTATION_NATIVE)
#include "../examples/gui_presentation/gui_application.hpp"
#endif

namespace {

using namespace truffle;

renderer::specialist::FrameGraph window_pass() {
  renderer::specialist::FrameGraph graph;
  graph.add_node(std::make_unique<renderer::specialist::RenderPassNode>(
      true, std::vector<renderer::specialist::RenderBatch>{}));
  return graph;
}

// Fail after acquisition, without adding fault injection to production APIs.
class FailingCache final : public renderer::specialist::IPipelineCache {
public:
  base::Status failure =
      base::Status::failure(base::StatusCode::backend_error, "test recording failure");
  base::Status prepare_render_pass(const rhi::RenderPassDesc &) override {
    return failure;
  }
  base::Result<rhi::Pipeline *> get_or_create(const renderer::specialist::InstanceLayout &,
                                              renderer::specialist::MaterialId,
                                              std::size_t) override {
    return failure;
  }
  void invalidate(renderer::specialist::MaterialId) override {}
  void invalidate_all() override {}
};

} // namespace

int main() {
  auto context = tests::make_null_context();
  auto &device = context.device;
  const renderer::specialist::PresentationDesc desc{.extent = {64, 32}};
  rhi::Device invalid;
  TFL_CHECK(!renderer::specialist::Presentation::create(invalid, desc).ok());
  TFL_CHECK(!renderer::specialist::Presentation::create(device, {.framesInFlight = 0}).ok());
  TFL_CHECK(!renderer::specialist::Presentation::create(device, {.imageCount = 0}).ok());
  TFL_CHECK(!renderer::specialist::Presentation::create(
      device, {.native = {.kind = static_cast<renderer::WindowKind>(999)}}).ok());
  TFL_CHECK(!renderer::specialist::Presentation::create(
                 device, {.native = {.kind = renderer::WindowKind::win32}})
                 .ok());
  renderer::specialist::Presentation empty;
  TFL_CHECK(!empty.valid());
  TFL_CHECK(!empty.begin_frame().ok());
  TFL_CHECK(!empty.wait_idle().ok());

  auto result = renderer::specialist::Presentation::create(device, desc);
  TFL_CHECK(result.ok());
  auto presentation = std::move(result).value();
  renderer::specialist::Renderer renderer(device);
  const auto graph = window_pass();
  TFL_CHECK(&presentation.device() == &device);
  TFL_CHECK(presentation.swapchain() != nullptr);
  TFL_CHECK(!presentation.render(renderer, graph).ok());
  TFL_CHECK(!presentation.cancel_frame().ok());
  for (std::uint32_t index = 0; index < 4; ++index) {
    auto frame = presentation.begin_frame();
    TFL_CHECK(frame.ok() && frame.value().slot == index % 2);
    TFL_CHECK((frame.value().extent == rhi::Extent2D{64, 32}));
    TFL_CHECK(!presentation.begin_frame().ok());
    TFL_CHECK(!presentation.resize({96, 48}).ok());
    TFL_CHECK(!presentation.wait_idle().ok());
    TFL_CHECK(presentation.render(renderer, graph).ok());
    TFL_CHECK(presentation.last_frame_stats().submitted);
    TFL_CHECK(presentation.last_frame_stats().presented);
  }
  TFL_CHECK(presentation.wait_idle(std::chrono::milliseconds(1)).ok());

  // Minimize neither acquires nor consumes a streamed slot.
  TFL_CHECK(presentation.resize({0, 32}).ok());
  const auto suspendedStats = context.instance.stats();
  TFL_CHECK(presentation.begin_frame().status().code == base::StatusCode::out_of_date);
  TFL_CHECK(presentation.begin_frame().status().code == base::StatusCode::out_of_date);
  TFL_CHECK(context.instance.stats().swapchainsCreated ==
            suspendedStats.swapchainsCreated);
  TFL_CHECK(!presentation.last_frame_stats().submitted);
  TFL_CHECK(presentation.resize({96, 48}).ok());
  auto restored = presentation.begin_frame();
  TFL_CHECK(restored.ok() && restored.value().slot == 0);
  TFL_CHECK((restored.value().extent == rhi::Extent2D{96, 48}));
  TFL_CHECK(presentation.cancel_frame().ok());

  // No-window graphs and foreign Renderers are errors, never headless success.
  TFL_CHECK(presentation.begin_frame().ok());
  TFL_CHECK(presentation.render(renderer, {}).code == base::StatusCode::invalid_argument);
  auto other = tests::make_null_context();
  renderer::specialist::Renderer foreign(other.device);
  TFL_CHECK(presentation.begin_frame().ok());
  TFL_CHECK(presentation.render(foreign, graph).code ==
            base::StatusCode::invalid_argument);
  TFL_CHECK(presentation.wait_idle().ok());

  // A failed recording may leave an acquired image. The next frame recreates
  // its swapchain instead of attempting an illegal second acquisition.
  FailingCache cache;
  renderer::specialist::Renderer failing(device, &cache);
  const auto previous = context.instance.stats().swapchainsCreated;
  TFL_CHECK(presentation.begin_frame().ok());
  TFL_CHECK(presentation.render(failing, graph).code == base::StatusCode::backend_error);
  TFL_CHECK(!presentation.last_frame_stats().submitted);
  TFL_CHECK(presentation.begin_frame().ok());
  TFL_CHECK(context.instance.stats().swapchainsCreated == previous + 1);
  TFL_CHECK(presentation.render(renderer, graph).ok());

  // Surface loss is host-actionable; it is not silently retried or hidden.
  cache.failure =
      base::Status::failure(base::StatusCode::surface_lost, "test lost host surface");
  TFL_CHECK(presentation.begin_frame().ok());
  TFL_CHECK(presentation.render(failing, graph).code == base::StatusCode::surface_lost);
  TFL_CHECK(presentation.begin_frame().status().code == base::StatusCode::surface_lost);
  TFL_CHECK(presentation.replace_surface({}, {80, 40}).ok());
  TFL_CHECK(presentation.begin_frame().ok());
  TFL_CHECK(presentation.render(renderer, graph).ok());

  // Creation while minimized is valid but performs no surface allocation.
  const auto surfaces = context.instance.stats().surfacesCreated;
  auto minimized = renderer::specialist::Presentation::create(device, {.extent = {0, 0}});
  TFL_CHECK(minimized.ok() && minimized.value().swapchain() == nullptr);
  TFL_CHECK(context.instance.stats().surfacesCreated == surfaces);
  TFL_CHECK(minimized.value().begin_frame().status().code ==
            base::StatusCode::out_of_date);

  // Owned-device addresses remain stable across moves, including Renderer use.
  auto instance = rhi::create_null_instance();
  TFL_CHECK(instance.ok());
  auto ownedResult = renderer::specialist::Presentation::create(std::move(instance).value(), desc);
  TFL_CHECK(ownedResult.ok());
  auto owned = std::move(ownedResult).value();
  auto *ownedDevice = &owned.device();
  renderer::specialist::Renderer shared(*ownedDevice);
  auto moved = std::move(owned);
  TFL_CHECK(!owned.valid() && &moved.device() == ownedDevice);
  TFL_CHECK(moved.begin_frame().ok());
  TFL_CHECK(moved.render(shared, graph).ok());
  TFL_CHECK(moved.wait_idle().ok());

#if defined(TFL_TEST_PRESENTATION_NATIVE)
  TFL_CHECK(renderer::specialist::create_native_presentation(desc).status().code ==
            base::StatusCode::unsupported);
  TFL_CHECK(renderer::specialist::create_native_presentation(
                desc, {.backend = rhi::BackendKind::null_validation})
                .status()
                .code == base::StatusCode::unsupported);
  TFL_CHECK(
      renderer::specialist::create_native_presentation(desc, {.backend = rhi::BackendKind::opengl})
          .status()
          .code == base::StatusCode::unsupported);
#endif
}
