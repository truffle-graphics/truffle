#include "../examples/gui_presentation/gui_application.hpp"
#include "../examples/gui_presentation/shared_device.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/rhi_interop.hpp"

#include <limits>

int main() {
  using namespace truffle;
  gui::Application empty;
  TFL_CHECK(!empty.valid() && !empty.needs_frame());
  TFL_CHECK(!empty.draw().ok() && !empty.handle(gui::FocusEvent{false}).ok());
  TFL_CHECK(!empty.resize({10, 10}, 1).ok());
  TFL_CHECK(!empty.load_font({}, 12).ok());
  TFL_CHECK(!empty.add_image({}).ok());
  TFL_CHECK(!gui::Application::create({.dpi_scale = 0}).ok());
  TFL_CHECK(
      !gui::Application::create({.dpi_scale = std::numeric_limits<float>::infinity()})
           .ok());
  TFL_CHECK(gui::Application::create({}).status().code == base::StatusCode::unsupported);
  auto gpu = tests::make_null_context();
  auto device = renderer::RhiInterop::borrow(gpu.device);
  TFL_CHECK(device.ok());
  const auto borrowed = gui::Rendering::create(device.value(), {.pixels = {0, 0}});
  TFL_CHECK(!borrowed.ok() && borrowed.status().code == base::StatusCode::unsupported);
  gui::Context context;
  TFL_CHECK(context.decoration(context.root()).value().fill.a == 0);
  TFL_CHECK(context.set_viewport({100, 50}, 2).ok());
  TFL_CHECK(context.prepare_frame(gui::Context::Clock::now()).status.ok());
}
