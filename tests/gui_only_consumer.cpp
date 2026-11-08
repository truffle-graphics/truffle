#include "../examples/gui_presentation/gui_graphics.hpp"
#include "test_support.hpp"
#include "truffle/gui/collections.hpp"
#include "truffle/gui/region.hpp"
#include "truffle/gui/text_edit.hpp"
#include "truffle/gui/ui.hpp"
#include "truffle/gui/viewport.hpp"

int main() {
  using namespace truffle;
  auto result = gui::Graphics::create({});
  TFL_CHECK(!result.ok() && result.status().code == base::StatusCode::unsupported);
  gui::Context context;
  TFL_CHECK(context.decoration(context.root()).value().fill.a == 0);
  gui::Context menus; // Ui gives its root a stack; keep it off the placed viewport below
  gui::Ui ui{menus};
  TFL_CHECK(ui.root().menu_bar().valid());
  auto viewport = gui::Viewport::create(context, context.root());
  TFL_CHECK(viewport.ok());
  TFL_CHECK(context.set_viewport({32, 24}, 2).ok());
  TFL_CHECK(context
                .set_layout_params(viewport.value().element(),
                                   {
                                       .width = 10.25F,
                                       .height = 6.25F,
                                       .position = {2, 2},
                                   })
                .ok());
  TFL_CHECK(context.prepare_frame(gui::Context::Clock::now()).status.ok());
  auto metrics = viewport.value().metrics();
  TFL_CHECK(metrics.ok() && metrics.value().drawable);
  TFL_CHECK((metrics.value().pixels == gui::PixelSize{21, 13}));
  bool routed = false;
  TFL_CHECK(viewport.value()
                .set_event_handler([&](gui::Context &, gui::ViewportEvent &input) {
                  if (input.routed.input<gui::PointerEvent>())
                    routed = input.local == gui::Point{1, 2} &&
                             input.pixels == gui::Point{2, 4};
                })
                .ok());
  TFL_CHECK(context
                .handle(gui::PointerEvent{
                    .action = gui::PointerAction::down,
                    .position = {3, 4},
                    .button = gui::PointerButton::primary,
                })
                .ok());
  TFL_CHECK(routed && context.focused() == viewport.value().element());
  TFL_CHECK(context.destroy(viewport.value().element()).ok());
  TFL_CHECK(!viewport.value().alive() && !viewport.value().metrics().ok());
  gui::Viewport stale;
  {
    gui::Context owner;
    auto child = gui::Viewport::create(owner, owner.root());
    TFL_CHECK(child.ok());
    stale = child.value();
  }
  TFL_CHECK(!stale.alive() && !stale.request_redraw().ok());
}
