#include "menu_internal.hpp"

#include "truffle/gui/themes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace truffle::gui {
namespace {

enum class Indicator { none, check, right, down, separator };

class IndicatorBuilder final : public ShapeBuilder {
public:
  explicit IndicatorBuilder(Indicator kind) : kind_(kind) {}
  void build(ShapeWriter &out, Size size, const Decoration &look) const override {
    const float x = size.width * 0.5F;
    const float y = size.height * 0.5F;
    const float sx = size.width / 12;
    const float sy = size.height / 12;
    if (kind_ == Indicator::check) {
      const float stroke = 1.5F * std::min(sx, sy);
      out.line({x - 4 * sx, y}, {x - sx, y + 3 * sy}, stroke, look.fill);
      out.line({x - sx, y + 3 * sy}, {x + 4 * sx, y - 4 * sy}, stroke, look.fill);
    } else if (kind_ == Indicator::right || kind_ == Indicator::down) {
      const std::array<Point, 3> points =
          kind_ == Indicator::right
              ? std::array<Point, 3>{{{x - 3 * sx, y - 4 * sy},
                                      {x + 3 * sx, y}, {x - 3 * sx, y + 4 * sy}}}
              : std::array<Point, 3>{{{x - 4 * sx, y - 2 * sy},
                                      {x + 4 * sx, y - 2 * sy}, {x, y + 3 * sy}}};
      constexpr std::array<std::uint32_t, 3> indices{0, 1, 2};
      out.triangles(points, indices, look.fill);
    } else if (kind_ == Indicator::separator) {
      out.rect({0, std::floor(y), size.width, 1}, {.fill = look.fill});
    }
  }

private:
  Indicator kind_;
};

class MenuLayout final : public Layout {
public:
  explicit MenuLayout(std::weak_ptr<detail::MenuState> menu) : menu_(std::move(menu)) {}
  Size measure(LayoutPass &pass, ElementId element, Constraints content) const override {
    return StackLayout{}.measure(pass, element, content);
  }
  void arrange(LayoutPass &pass, ElementId element, Rect content) const override {
    StackLayout{}.arrange(pass, element, content);
    if (const auto menu = menu_.lock(); menu && menu->live() && menu->open) {
      pass.request_update(menu->session->owner);
    }
  }

private:
  std::weak_ptr<detail::MenuState> menu_;
};

base::Status label(Context &context, ElementId parent, MenuTextStyle style,
                   const std::string &text, std::uint16_t column, Widget &out) {
  auto made = Widget::create(context, parent,
                             {.kind = WidgetKind::label,
                              .text = text,
                              .text_engine = style.text_engine,
                              .font = style.font,
                              .atlas = style.atlas});
  if (!made.ok()) {
    return made.status();
  }
  out = made.value();
  LayoutParams params;
  params.cell.column = column;
  params.cell.vertical = Align::center;
  auto status = context.set_layout_params(out.element(), params);
  if (status.ok()) {
    status = context.bind_component_property(out.element(), Property::text_color, parent);
  }
  if (status.ok()) {
    status = context.bind_component_property(out.element(), Property::font_size, parent);
  }
  return status;
}

base::Status indicator(Context &context, ElementId parent, std::uint16_t column,
                       Indicator kind, ElementId &out) {
  const auto made = context.create(parent);
  if (!made.ok()) {
    return made.status();
  }
  out = made.value();
  const auto size = context.component_property(parent, Property::indicator_size);
  if (!size.ok()) {
    return size.status();
  }
  const auto dimensions = std::get<Size>(size.value());
  LayoutParams params;
  params.width = dimensions.width;
  params.height = dimensions.height;
  params.cell.column = column;
  params.cell.horizontal = Align::center;
  params.cell.vertical = Align::center;
  auto status = context.set_layout_params(out, params);
  if (status.ok()) {
    status = context.set_interaction(out, {.hit_test = false});
  }
  if (status.ok()) {
    status = context.bind_component_property(out, Property::fill, parent, StylePart::indicator);
  }
  if (status.ok()) {
    status = context.set_shape_builder(out, std::make_shared<IndicatorBuilder>(kind));
  }
  return status;
}

base::Result<detail::MenuGeometry> menu_geometry(Context &context, ElementId element,
                                                bool item, bool resolved, bool single_column) {
  const auto value = [&](Property property) {
    return resolved ? context.property(element, property)
                    : context.component_property(element, property);
  };
  const auto indicator_size = value(Property::indicator_size);
  const auto trailing = value(Property::menu_trailing_width);
  const auto gap = value(Property::menu_column_gap);
  if (!indicator_size.ok()) { return indicator_size.status(); }
  if (!trailing.ok()) { return trailing.status(); }
  if (!gap.ok()) { return gap.status(); }
  float leading = 0;
  if (item) {
    const auto width = value(Property::menu_leading_width);
    if (!width.ok()) { return width.status(); }
    leading = std::get<float>(width.value());
  }
  return detail::MenuGeometry{std::get<Size>(indicator_size.value()), leading,
                              std::get<float>(trailing.value()), std::get<float>(gap.value()),
                              single_column};
}

base::Status apply_menu_geometry(Context &context, ElementId element, ElementId mark,
                                 ElementId arrow, bool item, detail::MenuGeometry geometry) {
  GridOptions options;
  if (geometry.single_column) {
    options.columns = {Track::content()};
  } else if (item) {
    options.columns = {Track::fixed(geometry.leading), Track::weighted(), Track::content(),
                       Track::fixed(geometry.trailing)};
    options.column_spacing = geometry.gap;
  } else {
    options.columns = {Track::weighted(), Track::fixed(geometry.trailing)};
    options.column_spacing = geometry.gap;
  }
  auto status = context.set_layout(element, std::make_shared<GridLayout>(std::move(options)));
  for (const auto indicator_id : {mark, arrow}) {
    if (!status.ok() || !indicator_id.valid()) { continue; }
    const auto current = context.layout_params(indicator_id);
    if (!current.ok()) { return current.status(); }
    auto params = current.value();
    params.width = geometry.indicator.width;
    params.height = geometry.indicator.height;
    status = context.set_layout_params(indicator_id, params);
  }
  return status;
}

base::Status remove_after_failure(Context &context, ElementId element,
                                  base::Status status) {
  const auto removed = context.destroy(element);
  return removed.ok() ? status : removed;
}

} // namespace

namespace detail {

base::Status invalid_menu() {
  return base::Status::failure(base::StatusCode::invalid_argument,
                               "GUI menu reference is stale or has the wrong kind");
}

bool menu_descendant(const Context &context, ElementId element, ElementId ancestor) {
  while (context.alive(element)) {
    if (element == ancestor) {
      return true;
    }
    const auto parent = context.parent(element);
    if (!parent.ok()) {
      break;
    }
    element = parent.value();
  }
  return false;
}

bool menu_input_enabled(const Context &context, ElementId element) {
  if (!context.visible(element)) {
    return false;
  }
  while (context.alive(element)) {
    const auto interaction = context.interaction(element);
    if (!interaction.ok() || !interaction.value().enabled) {
      return false;
    }
    const auto parent = context.parent(element);
    if (!parent.ok()) {
      return false;
    }
    if (!parent.value().valid()) {
      return true;
    }
    element = parent.value();
  }
  return false;
}

bool MenuState::live() const {
  return !retired && session && session->context && session->context->alive(element);
}
bool MenuItemState::live() const {
  return !retired && session && session->context && session->context->alive(element);
}
bool MenuButtonState::live() const {
  return !retired && session && session->context && session->context->alive(element);
}
bool MenuBarState::live() const {
  return !retired && session && session->context && session->context->alive(element);
}
bool MenuItemState::eligible() const {
  return live() && !separator && desc.enabled &&
         (desc.activated || (submenu && submenu->live()));
}

base::Status MenuItemState::interaction() {
  if (!live()) {
    return invalid_menu();
  }
  return session->context->set_interaction(
      element,
      {.hit_test = !separator, .enabled = desc.enabled, .focusable = eligible()});
}

base::Status MenuItemState::appearance() {
  if (!live()) {
    return invalid_menu();
  }
  auto &context = *session->context;
  if (separator) {
    return base::Status::success();
  }
  const bool active = context.focused() == element || (submenu && submenu->open);
  const bool checked = desc.checked.value_or(false);
  auto status = context.set_component_state(element, {
      .hovered = menu_descendant(context, context.hovered(), element),
      .pressed = session->pressed.lock().get() == this,
      .focused = active, .disabled = !desc.enabled, .checked = checked});
  if (status.ok() && painted_check != checked) {
    status = context.set_shape_builder(
        mark,
        std::make_shared<IndicatorBuilder>(checked ? Indicator::check : Indicator::none));
    if (status.ok()) {
      painted_check = checked;
    }
  }
  const bool has_submenu = submenu && submenu->live();
  if (status.ok() && painted_arrow != has_submenu) {
    status = context.set_shape_builder(
        arrow, std::make_shared<IndicatorBuilder>(has_submenu ? Indicator::right
                                                              : Indicator::none));
    if (status.ok()) {
      painted_arrow = has_submenu;
    }
  }
  return status;
}

base::Status MenuItemState::style_changed() {
  auto &context = *session->context;
  const auto next = menu_geometry(context, element, true, true, false);
  if (!next.ok()) { return next.status(); }
  if (geometry == next.value()) { return base::Status::success(); }
  const auto status = apply_menu_geometry(context, element, mark, arrow, true, next.value());
  if (status.ok()) { geometry = next.value(); }
  return status;
}

base::Status MenuItemState::event(RoutedEvent &event) {
  if (!live()) {
    return base::Status::success();
  }
  if (std::get_if<HoverEvent>(&event.payload)) {
    return appearance();
  }
  if (std::get_if<ElementFocusEvent>(&event.payload)) {
    auto status = appearance();
    if (status.ok()) {
      status = session->focus_changed();
    }
    return status;
  }
  return base::Status::success();
}

base::Status MenuButtonState::appearance() {
  if (!live()) {
    return invalid_menu();
  }
  auto &context = *session->context;
  const auto interaction = context.interaction(element);
  if (!interaction.ok()) {
    return interaction.status();
  }
  return context.set_component_state(element, {
      .hovered = hover || menu->open, .pressed = pointer.has_value() || space_pressed,
      .focused = context.focused() == element, .disabled = !interaction.value().enabled});
}

base::Status MenuButtonState::style_changed() {
  auto &context = *session->context;
  const auto next = menu_geometry(context, element, false, true, !bar.expired());
  if (!next.ok()) { return next.status(); }
  if (geometry == next.value()) { return base::Status::success(); }
  const auto status = apply_menu_geometry(context, element, {}, arrow, false, next.value());
  if (status.ok()) { geometry = next.value(); }
  return status;
}

base::Status MenuButtonState::event(RoutedEvent &event) {
  if (!live()) {
    return base::Status::success();
  }
  auto &context = *session->context;
  if (const auto *change = std::get_if<HoverEvent>(&event.payload)) {
    hover = change->entered;
    return appearance();
  }
  if (const auto *change = std::get_if<ElementFocusEvent>(&event.payload)) {
    if (!change->focused) {
      space_pressed = false;
    }
    auto status = appearance();
    if (status.ok()) {
      status = session->focus_changed();
    }
    return status;
  }
  if (const auto *lost = std::get_if<CaptureLostEvent>(&event.payload)) {
    if (pointer == lost->pointer) {
      pointer.reset();
    }
    return appearance();
  }
  if (const auto *input = event.input<PointerEvent>()) {
    if (input->action == PointerAction::down && input->button == PointerButton::primary &&
        !pointer) {
      pointer = input->pointer;
      auto status = context.capture_pointer(element, input->pointer);
      if (!status.ok()) {
        pointer.reset();
        return status;
      }
      event.prevent_default();
      event.stop_propagation();
      return appearance();
    }
    if (input->action == PointerAction::up && input->button == PointerButton::primary &&
        pointer == input->pointer) {
      pointer.reset();
      event.prevent_default();
      event.stop_propagation();
      auto status = appearance();
      if (!status.ok()) {
        return status;
      }
      if (menu_descendant(context, context.hit_test(input->position), element)) {
        status = MenuSession::sync(shared_from_this());
        if (!status.ok()) {
          return status;
        }
        return menu->open ? session->close_from(0)
                          : session->open_root(shared_from_this());
      }
    }
  } else if (const auto *key = event.input<KeyEvent>()) {
    if (key->key == Key::space) {
      event.prevent_default();
      event.stop_propagation();
      if (key->down && !key->repeat) {
        space_pressed = true;
      } else if (!key->down && space_pressed) {
        space_pressed = false;
        auto status = MenuSession::sync(shared_from_this());
        if (!status.ok()) {
          return status;
        }
        return menu->open ? session->close_from(0)
                          : session->open_root(shared_from_this());
      }
      return appearance();
    }
    if (!key->down) {
      return base::Status::success();
    }
    if (key->key == Key::enter || key->key == Key::down || key->key == Key::up) {
      event.prevent_default();
      event.stop_propagation();
      if (key->repeat && key->key == Key::enter) {
        return base::Status::success();
      }
      auto status = MenuSession::sync(shared_from_this());
      return status.ok() ? session->open_root(shared_from_this(), key->key == Key::up)
                         : status;
    }
    if (key->key == Key::left || key->key == Key::right) {
      event.prevent_default();
      event.stop_propagation();
      return session->traverse_bar(shared_from_this(), key->key == Key::left ? -1 : 1,
                                   false);
    }
  }
  return base::Status::success();
}

} // namespace detail

base::Result<Menu> Menu::create(Context &context, ElementId parent, MenuTextStyle text) {
  const auto session = detail::MenuSession::get(context, parent);
  if (!session.ok()) {
    return session.status();
  }
  auto status = install_default_theme(context);
  if (!status.ok()) {
    return status;
  }
  const auto made = context.create(parent);
  if (!made.ok()) {
    return made.status();
  }
  auto state = std::make_shared<detail::MenuState>();
  state->session = session.value();
  state->element = made.value();
  state->text = text;
  const auto setup = [&]() {
    auto result = context.set_component(state->element, Component::menu);
    if (result.ok()) {
      result = context.set_visible(state->element, false);
    }
    if (result.ok()) {
      result = context.set_interaction(state->element, {.focusable = true});
    }
    if (result.ok()) {
      result = context.set_semantics(state->element, {Context::Role::menu, {}});
    }
    if (result.ok()) {
      result = context.set_layout(state->element, std::make_shared<MenuLayout>(state));
    }
    for (const auto property : {Property::fill, Property::border_color,
                                Property::border_width, Property::corner_radius,
                                Property::padding, Property::min_size}) {
      if (result.ok()) {
        result = context.bind_component_property(state->element, property, state->element);
      }
    }
    if (result.ok()) {
      result = context.set_event_handler(
          state->element, [state](Context &, RoutedEvent &event) {
            if (std::get_if<ElementFocusEvent>(&event.payload)) {
              event.status = state->session->focus_changed();
            }
          });
    }
    if (result.ok()) {
      result =
          context.set_cleanup(state->element, [state] { state->session->retire(state); });
    }
    return result;
  };
  status = setup();
  if (!status.ok()) {
    return remove_after_failure(context, state->element, status);
  }
  state->session->menus.push_back(state);
  return Menu{std::move(state)};
}

base::Result<MenuItem> Menu::add_item(MenuItemDesc desc) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  const auto synced = detail::MenuSession::sync(state_);
  if (!synced.ok()) {
    return synced;
  }
  auto &context = *state_->session->context;
  const auto made = context.create(element());
  if (!made.ok()) {
    return made.status();
  }
  auto item = std::make_shared<detail::MenuItemState>();
  item->session = state_->session;
  item->parent = state_;
  item->element = made.value();
  item->desc = std::move(desc);
  const auto setup = [&]() {
    auto status = context.set_component(item->element, Component::menu_item);
    if (status.ok()) {
      status = context.set_semantics(item->element, {Context::Role::menu_item, item->desc.text});
    }
    for (const auto property : {Property::fill, Property::border_color,
                                Property::border_width, Property::corner_radius,
                                Property::padding, Property::min_size, Property::indicator_size,
                                Property::menu_leading_width, Property::menu_trailing_width,
                                Property::menu_column_gap}) {
      if (status.ok()) {
        status = context.bind_component_property(item->element, property, item->element);
      }
    }
    if (status.ok()) {
      status =
          label(context, item->element, state_->text, item->desc.text, 1, item->label);
    }
    if (status.ok()) {
      status = label(context, item->element, state_->text, item->desc.shortcut, 2,
                     item->shortcut);
    }
    if (status.ok()) {
      status = indicator(context, item->element, 0, Indicator::none, item->mark);
    }
    if (status.ok()) {
      status = indicator(context, item->element, 3, Indicator::none, item->arrow);
    }
    if (status.ok()) {
      const auto geometry = menu_geometry(context, item->element, true, false, false);
      if (!geometry.ok()) {
        status = geometry.status();
      } else {
        status = apply_menu_geometry(context, item->element, item->mark, item->arrow,
                                     true, geometry.value());
        if (status.ok()) { item->geometry = geometry.value(); }
      }
    }
    if (status.ok()) {
      status = item->interaction();
    }
    if (status.ok()) {
      status = item->appearance();
    }
    if (status.ok()) {
      status = context.set_component_style_handler(
          item->element, [item] { return item->style_changed(); });
    }
    if (status.ok()) {
      status =
          context.set_event_handler(item->element, [item](Context &, RoutedEvent &event) {
            event.status = item->event(event);
          });
    }
    if (status.ok()) {
      status =
          context.set_cleanup(item->element, [item] { item->session->retire(item); });
    }
    return status;
  };
  const auto status = setup();
  if (!status.ok()) {
    return remove_after_failure(context, item->element, status);
  }
  state_->items.push_back(item);
  return MenuItem{std::move(item)};
}

base::Result<Menu> Menu::add_submenu(std::string text, bool enabled) {
  auto item = add_item({.text = std::move(text), .enabled = enabled});
  if (!item.ok()) {
    return item.status();
  }
  auto menu = create(*state_->session->context, item.value().element(), state_->text);
  auto status = menu.ok() ? item.value().set_submenu(menu.value()) : menu.status();
  if (!status.ok()) {
    return remove_after_failure(*state_->session->context, item.value().element(),
                                status);
  }
  return menu.value();
}

base::Result<MenuSeparator> Menu::add_separator() {
  if (!alive()) {
    return detail::invalid_menu();
  }
  const auto synced = detail::MenuSession::sync(state_);
  if (!synced.ok()) {
    return synced;
  }
  auto &context = *state_->session->context;
  const auto made = context.create(element());
  if (!made.ok()) {
    return made.status();
  }
  auto item = std::make_shared<detail::MenuItemState>();
  item->session = state_->session;
  item->parent = state_;
  item->element = made.value();
  item->separator = true;
  auto status = context.set_component(item->element, Component::menu_separator);
  if (status.ok()) {
    status = context.set_interaction(item->element, {.hit_test = false});
  }
  if (status.ok()) {
    status = context.set_semantics(item->element, {Context::Role::menu_separator, {}});
  }
  if (status.ok()) {
    status = context.bind_component_property(item->element, Property::fill, item->element);
  }
  if (status.ok()) {
    status = context.bind_component_property(item->element, Property::min_size, item->element);
  }
  if (status.ok()) {
    status = context.set_shape_builder(
        item->element, std::make_shared<IndicatorBuilder>(Indicator::separator));
  }
  if (status.ok()) {
    status = context.set_cleanup(item->element, [item] {
      item->retired = true;
      if (auto parent = item->parent.lock()) {
        std::erase(parent->items, item);
      }
    });
  }
  if (!status.ok()) {
    return remove_after_failure(context, item->element, status);
  }
  state_->items.push_back(item);
  return MenuSeparator{std::move(item)};
}

ElementId Menu::element() const noexcept {
  return state_ ? state_->element : ElementId{};
}
ElementId Menu::trigger() const noexcept {
  if (!alive()) {
    return {};
  }
  if (const auto button = state_->button.lock(); button && button->live()) {
    return button->element;
  }
  if (const auto item = state_->parent_item.lock(); item && item->live()) {
    return item->element;
  }
  return {};
}
bool Menu::alive() const noexcept { return state_ && state_->live(); }
bool Menu::is_open() const noexcept { return alive() && state_->open; }
base::Status Menu::open() {
  if (!alive()) {
    return detail::invalid_menu();
  }
  const auto state = state_;
  return state->session->run(element(), [state] {
    if (auto button = state->button.lock()) {
      auto status = detail::MenuSession::sync(button);
      return status.ok() ? button->session->open_root(button) : status;
    }
    if (auto item = state->parent_item.lock()) {
      return item->session->open_submenu(item);
    }
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI menu must be attached to a trigger before opening");
  });
}
base::Status Menu::close() {
  if (!alive()) {
    return detail::invalid_menu();
  }
  const auto state = state_;
  return state->session->run(element(), [state] {
    const auto index = state->session->index(state);
    return index ? state->session->close_from(*index) : base::Status::success();
  });
}

ElementId MenuItem::element() const noexcept {
  return state_ ? state_->element : ElementId{};
}
bool MenuItem::alive() const noexcept {
  return state_ && state_->live() && !state_->separator;
}
base::Status MenuItem::set_text(std::string text) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  auto status = state_->label.set_text(text);
  if (!status.ok()) {
    return status;
  }
  state_->desc.text = std::move(text);
  return state_->session->context->set_semantics(
      element(), {Context::Role::menu_item, state_->desc.text});
}
base::Result<std::string> MenuItem::text() const {
  if (!alive()) {
    return detail::invalid_menu();
  }
  return state_->desc.text;
}
base::Status MenuItem::set_enabled(bool enabled) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  const auto state = state_;
  return state->session->run(element(), [state, enabled] {
    state->desc.enabled = enabled;
    if (!enabled) {
      const auto status = state->session->cancel_pending(state);
      if (!status.ok()) {
        return status;
      }
    }
    if (!enabled &&
        detail::menu_descendant(*state->session->context,
                                state->session->context->focused(), state->element)) {
      if (auto parent = state->parent.lock()) {
        auto status = state->session->context->focus(state->element);
        if (status.ok()) {
          status = state->session->traverse(parent, 1);
        }
        if (!status.ok()) {
          return status;
        }
      }
    }
    if (!enabled && state->submenu) {
      if (const auto index = state->session->index(state->submenu)) {
        const auto status = state->session->close_from(*index, false);
        if (!status.ok()) {
          return status;
        }
      }
    }
    auto status = state->interaction();
    if (status.ok()) {
      status = state->appearance();
    }
    return status;
  });
}
base::Status MenuItem::set_checked(std::optional<bool> value) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  if (state_->desc.checked == value) {
    return base::Status::success();
  }
  state_->desc.checked = value;
  return state_->appearance();
}
base::Result<std::optional<bool>> MenuItem::checked() const {
  if (!alive()) {
    return detail::invalid_menu();
  }
  return state_->desc.checked;
}
base::Status MenuItem::set_shortcut(std::string text) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  const auto status = state_->shortcut.set_text(text);
  if (status.ok()) {
    state_->desc.shortcut = std::move(text);
  }
  return status;
}
base::Status MenuItem::set_action(std::function<void()> action) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  if (action && state_->submenu && state_->submenu->live()) {
    return base::Status::failure(
        base::StatusCode::invalid_state,
        "GUI menu item cannot have both an action and a submenu");
  }
  const auto state = state_;
  return state->session->run(element(), [state, action = std::move(action)]() mutable {
    state->desc.activated = std::move(action);
    if (!state->eligible()) {
      const auto status = state->session->cancel_pending(state);
      if (!status.ok()) {
        return status;
      }
    }
    if (!state->eligible() && state->session->context->focused() == state->element) {
      if (const auto parent = state->parent.lock(); parent && parent->open) {
        const auto status = state->session->traverse(parent, 1);
        if (!status.ok()) {
          return status;
        }
      }
    }
    return state->interaction();
  });
}
base::Status MenuItem::set_submenu(Menu menu) {
  if (!alive() || !menu.alive()) {
    return detail::invalid_menu();
  }
  if (state_->submenu == menu.state_) {
    return base::Status::success();
  }
  if (state_->desc.activated || (state_->submenu && state_->submenu->live()) ||
      !menu.state_->button.expired() || !menu.state_->parent_item.expired()) {
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI submenu must be unattached, local and action-free");
  }
  if (state_->session->context != menu.state_->session->context) {
    return detail::invalid_menu();
  }
  const auto parent = state_->parent.lock();
  auto synced = detail::MenuSession::sync(parent);
  if (synced.ok()) {
    synced = detail::MenuSession::sync(menu.state_);
  }
  if (!synced.ok()) {
    return synced;
  }
  if (state_->session != menu.state_->session) {
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI submenu must belong to the item's surface");
  }
  auto &context = *state_->session->context;
  auto status = context.reparent(menu.element(), element());
  if (!status.ok()) {
    return status;
  }
  status = context.set_popup(menu.element(), PopupSide::right);
  if (!status.ok()) {
    return status;
  }
  state_->submenu = menu.state_;
  menu.state_->parent_item = state_;
  status = state_->interaction();
  return status.ok() ? state_->appearance() : status;
}
base::Result<Menu> MenuItem::submenu() const {
  if (!alive() || !state_->submenu || !state_->submenu->live()) {
    return detail::invalid_menu();
  }
  return Menu{state_->submenu};
}
ElementId MenuSeparator::element() const noexcept {
  return state_ ? state_->element : ElementId{};
}
bool MenuSeparator::alive() const noexcept {
  return state_ && state_->live() && state_->separator;
}

base::Result<MenuButton> MenuButton::create(Context &context, ElementId parent,
                                            std::string text, MenuTextStyle style) {
  auto menu = Menu::create(context, parent, style);
  if (!menu.ok()) {
    return menu.status();
  }
  auto button = create(context, parent, std::move(text), menu.value());
  if (!button.ok()) {
    return remove_after_failure(context, menu.value().element(), button.status());
  }
  return button;
}

base::Result<MenuButton> MenuButton::create(Context &context, ElementId parent,
                                            std::string text, Menu menu) {
  if (!menu.alive() || menu.state_->session->context != &context) {
    return detail::invalid_menu();
  }
  const auto synced = detail::MenuSession::sync(menu.state_);
  if (!synced.ok()) {
    return synced;
  }
  const auto session = detail::MenuSession::get(context, parent);
  if (!session.ok()) {
    return session.status();
  }
  if (session.value() != menu.state_->session || !menu.state_->button.expired() ||
      !menu.state_->parent_item.expired()) {
    return base::Status::failure(
        base::StatusCode::invalid_state,
        "GUI menu button needs an unattached menu on its surface");
  }
  const auto made = context.create(parent);
  if (!made.ok()) {
    return made.status();
  }
  auto state = std::make_shared<detail::MenuButtonState>();
  state->session = session.value();
  state->element = made.value();
  state->menu = menu.state_;
  const auto setup = [&]() {
    auto status = context.set_component(state->element, Component::menu_button);
    if (status.ok()) {
      status = context.set_semantics(state->element, {Context::Role::button, text});
    }
    if (status.ok()) {
      status = context.set_interaction(state->element, {.focusable = true});
    }
    for (const auto property : {Property::fill, Property::border_color,
                                Property::border_width, Property::corner_radius,
                                Property::padding, Property::min_size, Property::indicator_size,
                                Property::menu_trailing_width, Property::menu_column_gap}) {
      if (status.ok()) {
        status = context.bind_component_property(state->element, property, state->element);
      }
    }
    if (status.ok()) {
      status = label(context, state->element, menu.state_->text, text, 0, state->label);
    }
    if (status.ok()) {
      status = indicator(context, state->element, 1, Indicator::down, state->arrow);
    }
    if (status.ok()) {
      const auto geometry = menu_geometry(context, state->element, false, false, false);
      if (!geometry.ok()) {
        status = geometry.status();
      } else {
        status = apply_menu_geometry(context, state->element, {}, state->arrow,
                                     false, geometry.value());
        if (status.ok()) { state->geometry = geometry.value(); }
      }
    }
    if (status.ok()) {
      status = state->appearance();
    }
    if (status.ok()) {
      status = context.set_component_style_handler(
          state->element, [state] { return state->style_changed(); });
    }
    if (status.ok()) {
      status = context.set_event_handler(
          state->element,
          [state](Context &, RoutedEvent &event) { event.status = state->event(event); });
    }
    return status;
  };
  auto status = setup();
  if (!status.ok()) {
    return remove_after_failure(context, state->element, status);
  }
  const ElementId old_parent = context.parent(menu.element()).value();
  status = context.reparent(menu.element(), state->element);
  if (status.ok()) {
    status = context.set_popup(menu.element(), PopupSide::below);
  }
  if (!status.ok()) {
    const auto restored = context.reparent(menu.element(), old_parent);
    return remove_after_failure(context, state->element,
                                restored.ok() ? status : restored);
  }
  menu.state_->button = state;
  status =
      context.set_cleanup(state->element, [state] { state->session->retire(state); });
  if (!status.ok()) {
    return remove_after_failure(context, state->element, status);
  }
  return MenuButton{std::move(state)};
}

ElementId MenuButton::element() const noexcept {
  return state_ ? state_->element : ElementId{};
}
bool MenuButton::alive() const noexcept { return state_ && state_->live(); }
base::Result<Menu> MenuButton::menu() const {
  if (!alive() || !state_->menu->live()) {
    return detail::invalid_menu();
  }
  return Menu{state_->menu};
}
base::Status MenuButton::set_text(std::string text) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  auto status = state_->label.set_text(text);
  if (!status.ok()) {
    return status;
  }
  return state_->session->context->set_semantics(
      element(), {Context::Role::button, std::move(text)});
}
base::Status MenuButton::set_enabled(bool enabled) {
  if (!alive() || !state_->menu->live()) {
    return detail::invalid_menu();
  }
  auto &context = *state_->session->context;
  auto interaction = context.interaction(element());
  if (!interaction.ok()) {
    return interaction.status();
  }
  interaction.value().enabled = enabled;
  auto status = context.set_interaction(element(), interaction.value());
  if (!status.ok()) {
    return status;
  }
  if (!enabled && state_->menu->open) {
    status = state_->session->close_from(0, false);
  }
  return status.ok() ? state_->appearance() : status;
}
base::Status MenuButton::open() {
  auto owned = menu();
  return owned.ok() ? owned.value().open() : owned.status();
}
base::Status MenuButton::close() {
  auto owned = menu();
  return owned.ok() ? owned.value().close() : owned.status();
}

base::Result<MenuBar> MenuBar::create(Context &context, ElementId parent,
                                      MenuTextStyle text) {
  const auto session = detail::MenuSession::get(context, parent);
  if (!session.ok()) {
    return session.status();
  }
  auto status = install_default_theme(context);
  if (!status.ok()) {
    return status;
  }
  const auto made = context.create(parent);
  if (!made.ok()) {
    return made.status();
  }
  auto state = std::make_shared<detail::MenuBarState>();
  state->session = session.value();
  state->element = made.value();
  state->text = text;
  status = context.set_component(state->element, Component::menu_bar);
  if (status.ok()) {
    status = context.set_semantics(state->element, {Context::Role::menu_bar, {}});
  }
  if (status.ok()) {
    status = context.set_interaction(state->element, {.hit_test = false});
  }
  if (status.ok()) {
    status = context.set_layout(state->element,
                                std::make_shared<StackLayout>(StackOptions{
                                    .axis = Axis::horizontal, .cross = Align::center}));
  }
  for (const auto property : {Property::fill, Property::border_color,
                              Property::border_width, Property::corner_radius,
                              Property::padding, Property::min_size}) {
    if (status.ok()) {
      status = context.bind_component_property(state->element, property, state->element);
    }
  }
  if (status.ok()) {
    status =
        context.set_cleanup(state->element, [state] { state->session->retire(state); });
  }
  if (!status.ok()) {
    return remove_after_failure(context, state->element, status);
  }
  state->session->bars.push_back(state);
  return MenuBar{std::move(state)};
}
ElementId MenuBar::element() const noexcept {
  return state_ ? state_->element : ElementId{};
}
bool MenuBar::alive() const noexcept { return state_ && state_->live(); }
base::Result<Menu> MenuBar::add_menu(std::string text) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  auto menu = Menu::create(*state_->session->context, element(), state_->text);
  if (!menu.ok()) {
    return menu.status();
  }
  const auto button = add_menu(std::move(text), menu.value());
  if (!button.ok()) {
    return remove_after_failure(*state_->session->context, menu.value().element(),
                                button.status());
  }
  return menu.value();
}
base::Result<MenuButton> MenuBar::add_menu(std::string text, Menu menu) {
  if (!alive()) {
    return detail::invalid_menu();
  }
  auto button =
      MenuButton::create(*state_->session->context, element(), std::move(text), menu);
  if (!button.ok()) {
    return button.status();
  }
  auto &context = *state_->session->context;
  auto status = context.set_visible(button.value().state_->arrow, false);
  if (status.ok()) {
    status = context.set_layout(
        button.value().element(),
        std::make_shared<GridLayout>(GridOptions{.columns = {Track::content()}}));
  }
  if (!status.ok()) {
    return remove_after_failure(context, button.value().element(), status);
  }
  button.value().state_->bar = state_;
  state_->buttons.push_back(button.value().state_);
  return button;
}

} // namespace truffle::gui
