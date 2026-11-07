#include "renderer_native_test_utils.hpp"
#include "truffle/gui/rendering.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/rhi_interop.hpp" // native setup/readback harness only

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

std::vector<std::byte> read_pixels(renderer::Device &shared,
                                   const renderer::Texture &target) {
  auto &device = renderer::RhiInterop::native(shared);
  auto &texture = *renderer::RhiInterop::native(target);
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
  if (texture.desc().format == rhi::TextureFormat::bgra8_unorm) {
    for (std::size_t y = 0; y < 16; ++y)
      for (std::size_t x = 0; x < 16; ++x)
        std::swap(bytes[y * 256 + x * 4], bytes[y * 256 + x * 4 + 2]);
  }
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
  auto native_device = tests::require(adapter.request_device());
  auto device = tests::require(renderer::RhiInterop::borrow(native_device));
  auto app = tests::require(gui::Rendering::create(device, {.pixels = {0, 0}}));
  auto pacer = tests::require(renderer::FramePacer::create(device));
  auto color = tests::require(
      renderer::SampledTexture::render_target(device, {.extent = {16, 16}}));
  auto &context = app.context();
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
    const auto frame =
        tests::require(gui::Rendering::prepare_viewport(app, viewport, slot));
    if (frame.redraw != (index != 1)) {
      throw std::runtime_error("viewport scene redraw/reuse contract changed");
    }
    renderer::FrameGraph graph;
    if (frame.redraw) {
      renderer::RenderPassDesc pass;
      pass.colorAttachments.push_back({.clear = {1, index == 3 ? 1.0F : 0.0F, 0, 1}});
      (void)tests::require(
          gui::Rendering::append_viewport_pass(viewport, graph, {}, std::move(pass)));
    }
    tests::require(gui::Rendering::display_viewport(app, viewport));
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
    std::vector<renderer::TextureTransition> transitions;
    if (index == 0)
      transitions.push_back(color.to_attachment());
    const auto guiPass = tests::require(gui::Rendering::append_gui_pass(
        app, graph, {slot, {16, 16}},
        {
            .colorAttachments = {{.texture = color.texture(), .clear = {0, 0, 1, 1}}},
        },
        false, nullptr, std::move(transitions)));
    tests::require(gui::Rendering::composite_viewport(viewport, graph, guiPass));
    auto &renderer = gui::Rendering::renderer(app);
    tests::require(renderer.render(graph, pacer.signal()));
    pacer.end_frame(renderer.last_frame_stats().submitted);
    tests::require(
        gui::Rendering::finish_viewport(viewport, renderer.last_frame_stats().submitted));
    tests::require(pacer.wait_idle());
    check_pixels(read_pixels(device, color.texture()), index == 3);
  }
  tests::require(gui::Rendering::release_viewport(viewport));
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
