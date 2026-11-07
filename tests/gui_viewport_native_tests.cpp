#include "renderer_native_test_utils.hpp"
#include "truffle/gui/viewport.hpp"
#include "truffle/renderer/specialist/frame_stream.hpp"

#if TFL_TEST_VIEWPORT_VULKAN
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"
#else
#include "truffle/rhi/metal_backend.hpp"
#endif

#include <chrono>

namespace {

using namespace truffle;

void place(gui::Context &context, gui::ElementId element, gui::Rect rect) {
  tests::require(context.set_layout_params(element, {
                                                        .width = rect.width,
                                                        .height = rect.height,
                                                        .position = {rect.x, rect.y},
                                                    }));
}

std::vector<std::byte> read_pixels(rhi::Device &device, rhi::Texture &texture) {
  auto readback = tests::require(device.create_buffer({
      .size = 256 * 16,
      .usage = rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::readback,
  }));
  auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
  auto list = tests::require(pool.allocate());
  tests::require(list.begin());
  tests::color_barrier(list, texture, rhi::TextureLayout::color_attachment,
                       rhi::TextureLayout::transfer_source);
  auto copy = tests::require(list.begin_copy());
  tests::require(
      copy.copy_texture_to_buffer(texture, readback,
                                  {
                                      .layout = {.bytesPerRow = 256, .rowsPerImage = 16},
                                      .texture = {.extent = {16, 16, 1}},
                                  }));
  tests::require(copy.end());
  tests::color_barrier(list, texture, rhi::TextureLayout::transfer_source,
                       rhi::TextureLayout::color_attachment);
  tests::require(list.end());
  auto fence = tests::require(device.create_fence());
  auto queue = tests::require(device.queue(rhi::QueueKind::graphics));
  const std::array<rhi::CommandList *, 1> lists{&list};
  tests::require(queue.submit(lists, &fence, 1));
  tests::require(fence.wait(1, std::chrono::seconds(10)));
  std::vector<std::byte> bytes(256 * 16);
  tests::require(readback.read(0, bytes));
  return bytes;
}

void check_pixels(const std::vector<std::byte> &bytes, bool yellow) {
  for (std::size_t y = 0; y < 16; ++y) {
    for (std::size_t x = 0; x < 16; ++x) {
      const bool overlay = x >= 6 && x < 10 && y >= 6 && y < 10;
      const bool clippedImage = x >= 4 && x < 12 && y >= 4 && y < 12;
      const std::array<unsigned, 4> expected =
          overlay        ? std::array<unsigned, 4>{0, 255, 0, 255}
          : clippedImage ? std::array<unsigned, 4>{255, yellow ? 255U : 0U, 0, 255}
                         : std::array<unsigned, 4>{0, 0, 255, 255};
      for (std::size_t channel = 0; channel < 4; ++channel) {
        if (std::to_integer<unsigned>(bytes[y * 256 + x * 4 + channel]) !=
            expected[channel]) {
          throw std::runtime_error("viewport clipped/overlay pixel mismatch at " +
                                   std::to_string(x) + "," + std::to_string(y));
        }
      }
    }
  }
}

int run() {
#if TFL_TEST_VIEWPORT_VULKAN
  auto instance = tests::require(rhi::create_vulkan_instance());
  if (rhi::detail::last_vulkan_validation_state_for_testing() !=
      rhi::detail::VulkanValidationState::active) {
    throw std::runtime_error("viewport Vulkan test requires active Khronos validation");
  }
#else
  auto instance = tests::require(rhi::create_metal_instance());
#endif
  auto adapter = tests::require(instance.adapter(0));
  auto device = tests::require(adapter.request_device());
  renderer::specialist::PipelineCache programs(device);
  renderer::specialist::Renderer renderer(device, &programs);
  auto painter = tests::require(gui::Painter::create(device, programs));
  auto pacer = tests::require(renderer::specialist::FramePacer::create(device));
  auto color = tests::make_color_target(device);
  gui::Context context;
  tests::require(context.set_viewport({16, 16}));
  const auto clip = tests::require(context.create(context.root()));
  place(context, clip, {4, 4, 8, 8});
  auto viewport = tests::require(gui::Viewport::create(context, clip));
  place(context, viewport.element(), {-4, -4, 16, 16});
  const auto overlay = tests::require(context.create(context.root()));
  place(context, overlay, {6, 6, 4, 4});
  tests::require(context.set_overlay(overlay, true));
  tests::require(context.set_decoration(overlay, {.fill = {0, 1, 0, 1}}));
  gui::TextureId stableId;
  for (std::uint32_t index = 0; index < 4; ++index) {
    if (index >= 2) {
      tests::require(viewport.request_redraw());
    }
    tests::require(context.prepare_frame(gui::Context::Clock::now()).status);
    const auto slot = tests::require(pacer.begin_frame());
    const auto frame = tests::require(viewport.prepare_frame(device, slot));
    if (frame.redraw != (index != 1)) {
      throw std::runtime_error("viewport scene redraw/reuse contract changed");
    }
    renderer::specialist::FrameGraph graph;
    if (frame.redraw) {
      rhi::RenderPassDesc pass;
      pass.colorAttachments.push_back({.clear = {1, index == 3 ? 1.0F : 0.0F, 0, 1}});
      (void)tests::require(viewport.append_render_pass(graph, {}, std::move(pass)));
    }
    tests::require(viewport.display(painter));
    const auto id = tests::require(context.decoration(viewport.element())).texture;
    if (index == 0) {
      stableId = id;
    } else if (id != stableId) {
      throw std::runtime_error("viewport texture rebinding changed its retained GUI ID");
    }
    const auto shapes = context.update_shapes();
    if (index != 0 && shapes.regenerated != 0) {
      throw std::runtime_error("scene-only redraw regenerated GUI shapes");
    }
    auto batches = tests::require(painter.record(context, slot, {16, 16}));
    const auto guiPass = graph.add_node(std::make_unique<renderer::specialist::RenderPassNode>(
        rhi::RenderPassDesc{
            .extent = {16, 16},
            .colorAttachments = {{.texture = &color, .clear = {0, 0, 1, 1}}}},
        std::move(batches)));
    tests::require(viewport.composite(graph, guiPass));
    tests::require(renderer.render(graph, nullptr, pacer.signal()));
    pacer.end_frame(renderer.last_frame_stats().submitted);
    tests::require(viewport.finish_frame(renderer.last_frame_stats().submitted));
    tests::require(pacer.wait_idle());
    check_pixels(read_pixels(device, color), index == 3);
  }
  tests::require(viewport.release_gpu());
  std::cout << "Viewport: 4 shared GPU frames, exact clips/overlays, retained ID and "
               "scene reuse\n";
  return 0;
}

} // namespace

int main() {
  try {
    return run();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
