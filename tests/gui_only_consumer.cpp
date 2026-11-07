#include "../examples/gui_presentation/gui_application.hpp"
#include "truffle/gui/builder.hpp"
#include "truffle/gui/collections.hpp"
#include "truffle/gui/menus.hpp"
#include "truffle/gui/region.hpp"
#include "truffle/gui/text_edit.hpp"
#include "test_support.hpp"

int main() {
    using namespace truffle;
    auto result = gui::Application::create({});
    TFL_CHECK(!result.ok() && result.status().code == base::StatusCode::unsupported);
    gui::Context context;
    TFL_CHECK(context.decoration(context.root()).value().fill.a == 0);
    auto menu = gui::MenuBar::create(context, context.root());
    TFL_CHECK(menu.ok());
}
