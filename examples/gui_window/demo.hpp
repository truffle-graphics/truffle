#pragma once

#include "truffle/gui/widgets.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace truffle::examples {

class GuiDemo {
public:
    gui::Context context;
    gui::Widget button;
    gui::Widget checkbox;
    gui::Widget slider;
    gui::Widget status;
    gui::Widget image;
    gui::Widget scroll;
    std::vector<gui::Widget> rows;
    int presses = 0;

    GuiDemo(gui::TextEngine& text, gui::FontId font, gui::TextureId atlas,
            gui::TextureId image_texture) {
        const auto root = context.root();
        check(context.set_layout(root, std::make_shared<gui::StackLayout>(
            gui::StackOptions{.spacing = 12})));
        gui::LayoutParams params;
        params.padding = {20, 20, 20, 20};
        check(context.set_layout_params(root, params));
        check(context.set_decoration(root, {.fill = {0.06F, 0.07F, 0.10F, 1}}));
        check(context.set_theme_value("demo.gap", 12.0F));

        const auto caption = [&](gui::ElementId parent, std::string value) {
            return take(gui::Widget::create(context, parent, {
                .kind = gui::WidgetKind::label, .text = std::move(value),
                .text_engine = &text, .font = font, .atlas = atlas}));
        };
        static_cast<void>(caption(root, "Truffle GUI - persistent, themed and interactive"));
        const auto body = take(context.create(root));
        params = {};
        params.weight = 1;
        check(context.set_layout_params(body, params));
        check(context.set_layout(body, std::make_shared<gui::StackLayout>(
            gui::StackOptions{.axis = gui::Axis::horizontal, .spacing = 16})));

        auto controls = take(gui::Widget::create(context, body));
        params = {};
        params.width = 280;
        check(context.set_layout_params(controls.element(), params));
        check(context.set_layout(controls.element(), std::make_shared<gui::StackLayout>()));
        check(context.bind_property(controls.element(), gui::Property::spacing, "demo.gap"));
        static_cast<void>(caption(controls.element(), "Core widgets"));
        status = caption(controls.element(), "Button presses: 0");
        button = take(gui::Widget::create(context, controls.element(), {
            .kind = gui::WidgetKind::button, .text = "Press me",
            .text_engine = &text, .font = font, .atlas = atlas,
            .activated = [this] {
                ++presses;
                check(status.set_text("Button presses: " + std::to_string(presses)));
            }}));
        checkbox = take(gui::Widget::create(context, controls.element(), {
            .kind = gui::WidgetKind::checkbox, .text = "Bright accent",
            .text_engine = &text, .font = font, .atlas = atlas, .checked = true,
            .toggled = [this](bool bright) {
                check(context.set_theme_value("gui.accent", bright
                    ? gui::Color{0.25F, 0.65F, 0.95F, 1} : gui::Color{0.65F, 0.4F, 0.8F, 1}));
            }}));
        static_cast<void>(caption(controls.element(), "Image tint - drag or use arrow keys"));
        slider = take(gui::Widget::create(context, controls.element(), {
            .kind = gui::WidgetKind::slider, .text = "Image tint", .value = 1, .step = 0.05F,
            .value_changed = [this](float value) {
                check(context.set_property_override(image.element(), gui::Property::fill,
                    gui::Color{value, 1, 1, 1}));
            }}));
        image = take(gui::Widget::create(context, controls.element(), {
            .kind = gui::WidgetKind::image, .text = "Application-provided checker image",
            .image = image_texture, .image_size = {128, 72}}));
        static_cast<void>(caption(controls.element(), "Tab / Shift+Tab: focus"));
        static_cast<void>(caption(controls.element(), "Space / Enter: activate"));

        scroll = take(gui::Widget::create(context, body, {
            .kind = gui::WidgetKind::scroll_area, .text = "Scrollable persistent elements"}));
        params = {};
        params.weight = 1;
        check(context.set_layout_params(scroll.element(), params));
        check(context.set_layout(scroll.content(), std::make_shared<gui::StackLayout>(
            gui::StackOptions{.spacing = 8})));
        for (int i = 0; i < 64; ++i) {
            auto row = take(gui::Widget::create(context, scroll.content()));
            params = {};
            params.height = 36;
            params.width = 390;
            check(context.set_layout_params(row.element(), params));
            check(context.set_property_override(row.element(), gui::Property::fill,
                i % 2 == 0 ? gui::Color{0.16F, 0.19F, 0.25F, 1}
                           : gui::Color{0.13F, 0.16F, 0.21F, 1}));
            static_cast<void>(caption(row.element(),
                "Persistent row " + std::to_string(i + 1) + " - scroll without rebuilding"));
            rows.push_back(std::move(row));
        }
        static_cast<void>(caption(root,
            "The host sleeps while idle. Frame work counters are printed to the terminal."));
    }

    void handle(const gui::InputEvent& event) { check(context.handle(event)); }

private:
    static void check(const base::Status& status) {
        if (!status.ok()) { throw std::runtime_error(status.message); }
    }
    template <typename T>
    static T take(base::Result<T> result) {
        if (!result.ok()) { throw std::runtime_error(result.status().message); }
        return std::move(result).value();
    }
};

} // namespace truffle::examples
