#include "../examples/gui_presentation/gui_graphics.hpp"
#include "../examples/gui_presentation/shared_device.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/rhi_interop.hpp"

#include <limits>

int main() {
  using namespace truffle;
  gui::Graphics empty;
  gui::Context context;
  TFL_CHECK(!empty.valid() && !empty.needs_frame(context));
  TFL_CHECK(!empty.draw(context).ok());
  TFL_CHECK(!empty.resize(context, {10, 10}, 1).ok());
  TFL_CHECK(!empty.load_font({}, 12).ok());
  TFL_CHECK(!empty.load_font({}).ok());
  TFL_CHECK(!empty.add_image(context, {}).ok());
  TFL_CHECK(!gui::Graphics::create({.dpi_scale = 0}).ok());
  TFL_CHECK(
      !gui::Graphics::create({.dpi_scale = std::numeric_limits<float>::infinity()})
           .ok());
  TFL_CHECK(gui::Graphics::create({}).status().code == base::StatusCode::unsupported);
  auto gpu = tests::make_null_context();
  auto device = renderer::RhiInterop::borrow(gpu.device);
  TFL_CHECK(device.ok());
  const auto borrowed = gui::Rendering::create(device.value(), {.pixels = {0, 0}});
  TFL_CHECK(!borrowed.ok() && borrowed.status().code == base::StatusCode::unsupported);
  TFL_CHECK(context.decoration(context.root()).value().fill.a == 0);
  TFL_CHECK(context.set_viewport({100, 50}, 2).ok());
  TFL_CHECK(context.prepare_frame(gui::Context::Clock::now()).status.ok());
}
