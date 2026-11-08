#pragma once

#include "truffle/gui/ui.hpp"

#include <string>
#include <vector>

namespace truffle::examples {

class GuiDemo {
public:
    gui::Context context;
    gui::Ui ui;
    gui::Button button;
    gui::Checkbox checkbox;
    gui::Slider slider;
    gui::Label status;
    gui::Image image;
    gui::Container scroll;
    gui::TextField editor;
    std::vector<gui::Container> rows;
    int presses = 0;

    GuiDemo(gui::TextEngine& text, gui::FontId font, gui::TextureId atlas,
            gui::TextureId image_texture)
        : ui(context, {&text, font, atlas}) {
        auto root = ui.root();
        root.padding(20).gap(12).on_paint([](gui::Painter& paint) {
            paint.rect({0, 0, paint.size().width, paint.size().height},
                       gui::Color{0.06F, 0.07F, 0.10F, 1});
        }, gui::PaintLayer::background);

        root.label("Truffle GUI - persistent, themed and interactive");
        auto body = root.row().weight(1).gap(16);

        auto controls = body.panel().width(280).gap(12);
        controls.label("Core widgets");
        status = controls.label("Button presses: 0");
        button = controls.button("Press me").on_click([this] {
            ++presses;
            status.text("Button presses: " + std::to_string(presses));
        });
        checkbox = controls.checkbox("Bright accent", true).on_change([this](bool bright) {
            static_cast<void>(context.set_theme_value("gui.accent", bright
                ? gui::Color{0.25F, 0.65F, 0.95F, 1} : gui::Color{0.65F, 0.4F, 0.8F, 1}));
        });
        controls.label("Image tint - drag or use arrow keys");
        slider = controls.slider(0, 1, 1).step(0.05F).on_change([this](float value) {
            image.property(gui::Property::fill, gui::Color{value, 1, 1, 1});
        });
        image = controls.image(image_texture, {128, 72});
        controls.label("Tab / Shift+Tab: focus");
        controls.label("Space / Enter: activate");
        controls.label("Editable text - clipboard, undo and IME");
        editor = controls.text_field(
            "Edit: \u05E9\u05DC\u05D5\u05DD \u0645\u0631\u062D\u0628\u0627 123");

        scroll = body.scroll().weight(1);
        for (int i = 0; i < 64; ++i) {
            auto row = scroll.panel().size({390, 36}).property(gui::Property::fill,
                i % 2 == 0 ? gui::Color{0.16F, 0.19F, 0.25F, 1}
                           : gui::Color{0.13F, 0.16F, 0.21F, 1});
            row.label("Persistent row " + std::to_string(i + 1) +
                      " - scroll without rebuilding");
            rows.push_back(row);
        }
        root.label("The host sleeps while idle. Frame work counters are printed to the terminal.");
    }

    void handle(const gui::InputEvent& event) {
        if (auto status = context.handle(event); !status.ok()) { ui.report(status); }
    }
};

} // namespace truffle::examples
