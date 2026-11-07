#include "truffle/gui/specialist/surface.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace truffle::gui {
namespace {
class SurfaceLayout final : public Layout {
public:
    Size measure(LayoutPass& pass, ElementId element, Constraints constraints) const override {
        for (auto child : pass.children(element)) {
            (void)pass.measure(child, Constraints::tight(constraints.max));
        }
        return constraints.clamp({});
    }
    void arrange(LayoutPass& pass, ElementId element, Rect content) const override {
        for (auto child : pass.children(element)) { pass.arrange(child, content); }
    }
};

base::Status invalid_surface() {
    return base::Status::failure(base::StatusCode::invalid_state, "GUI surface is not alive");
}
}

// Input and native services are per view; element storage, invalidation and
// updates stay in the Context. This scope never runs layout or user updaters.
class Surface::Scope {
public:
    explicit Scope(Surface& surface)
        : surface_(surface), root_(surface.context_->root_),
          viewport_(surface.context_->host_viewport_), entered_(root_ != surface.root_) {
        if (!entered_) { return; }
        surface.context_->root_ = surface.root_;
        for (const auto& entry : surface.context_->surface_roots_) {
            if (entry.root == surface.root_) {
                surface.context_->host_viewport_ = entry.size;
                break;
            }
        }
        swap();
    }
    ~Scope() {
        if (!entered_) { return; }
        auto& c = *surface_.context_;
        for (auto& entry : c.surface_roots_) {
            if (entry.root == surface_.root_ && entry.size != c.host_viewport_) {
                entry.size = c.host_viewport_;
                c.mark(entry.root, Dirty::measure);
            }
        }
        c.host_viewport_ = viewport_;
        swap();
        c.root_ = root_;
    }
private:
    void swap() {
        auto& c = *surface_.context_;
        std::swap(c.host_, surface_.host_);
        std::swap(c.scale_, surface_.scale_);
        std::swap(c.input_, surface_.input_);
        std::swap(c.hovered_, surface_.hovered_);
        std::swap(c.focused_, surface_.focused_);
        std::swap(c.captures_, surface_.captures_);
        std::swap(c.pointer_buttons_, surface_.pointer_buttons_);
        std::swap(c.event_status_, surface_.event_status_);
    }
    Surface& surface_;
    ElementId root_;
    Size viewport_;
    bool entered_;
};

base::Result<std::unique_ptr<Surface>> Surface::create(Context& context) {
    auto surface = std::unique_ptr<Surface>(new Surface(context));
    surface->root_ = context.allocate();
    context.surface_roots_.push_back({surface->root_, {}, surface.get()});
    context.mark(surface->root_, Dirty::style | Dirty::measure | Dirty::arrange | Dirty::shape);
    (void)context.set_layout(surface->root_, std::make_shared<SurfaceLayout>());
    return surface;
}

Surface::~Surface() {
    if (!alive()) { return; }
    (void)release_input();
    std::erase_if(context_->surface_roots_, [this](const auto& entry) {
        return entry.root == root_;
    });
    (void)context_->destroy(root_);
}

bool Surface::alive() const noexcept { return context_->alive(root_); }

void Surface::set_host(Host* host) noexcept {
    if (context_->root_ == root_) { context_->host_ = host; }
    else { host_ = host; }
}

base::Status Surface::set_viewport(Size size, float scale) {
    if (!alive()) { return invalid_surface(); }
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width < 0 || size.height < 0 || !std::isfinite(scale) || scale <= 0) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI surface viewport must be finite and nonnegative");
    }
    for (auto& [root, viewport, surface] : context_->surface_roots_) {
        (void)surface;
        if (root == root_ && viewport != size) {
            viewport = size;
            context_->mark(root_, Dirty::measure);
        }
    }
    auto& stored_scale = context_->root_ == root_ ? context_->scale_ : scale_;
    if (stored_scale != scale) { context_->request_frame(); stored_scale = scale; }
    if (context_->root_ == root_) { context_->host_viewport_ = size; }
    return base::Status::success();
}

base::Status Surface::attach(ElementId content) {
    if (!alive()) { return invalid_surface(); }
    if (!context_->alive(content) || content == context_->primary_root_ ||
        std::ranges::any_of(context_->surface_roots_, [content](const auto& entry) {
            return entry.root == content;
        })) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI surface needs a live non-root subtree");
    }
    const auto belongs = [&](ElementId element) {
        for (auto up = element; context_->alive(up);) {
            if (up == content) { return true; }
            up = context_->find(up)->parent;
        }
        return false;
    };
    const auto release = [&]() {
        if (belongs(context_->focused_)) {
            if (auto status = context_->focus({}); !status.ok()) { return status; }
        }
        std::vector<std::uint32_t> pointers;
        for (const auto& [pointer, element] : context_->captures_) {
            if (belongs(element)) { pointers.push_back(pointer); }
        }
        for (auto pointer : pointers) {
            if (auto status = context_->release_pointer(pointer); !status.ok()) { return status; }
        }
        if (belongs(context_->hovered_)) { context_->update_hover({}); }
        return base::Status::success();
    };
    if (auto* source = context_->surface_of(content); source != nullptr) {
        Scope scope(*source);
        if (auto status = release(); !status.ok()) { return status; }
    } else {
        if (auto status = release(); !status.ok()) { return status; }
    }
    return context_->reparent(content, root_);
}

base::Status Surface::release_input() {
    if (!alive()) { return invalid_surface(); }
    Scope scope(*this);
    auto status = context_->handle(FocusEvent{false});
    context_->host().set_cursor(Cursor::arrow);
    return status;
}

base::Status Surface::handle(const InputEvent& event, Context::Clock::time_point now) {
    if (!alive()) { return invalid_surface(); }
    Scope scope(*this);
    if (auto status = validate_input(); !status.ok()) { return status; }
    return context_->handle(event, now);
}

base::Status Surface::focus(ElementId element) {
    if (!alive()) { return invalid_surface(); }
    Scope scope(*this);
    return context_->focus(element);
}

base::Status Surface::validate_input() {
    // Property changes made outside view dispatch must also invalidate that
    // view's parked focus/capture state before its next event or update.
    return context_->set_interaction(root_, context_->find(root_)->interaction);
}

base::Status Surface::update(const Context::Updater& updater) {
    Scope scope(*this);
    if (auto status = validate_input(); !status.ok()) { return status; }
    return updater();
}

base::Status Surface::remove(ElementId element) {
    Scope scope(*this);
    return context_->destroy(element);
}

Surface* Context::surface_of(ElementId element) const noexcept {
    auto root = element;
    while (const auto* node = find(root)) {
        if (!node->parent.valid()) { break; }
        root = node->parent;
    }
    for (const auto& entry : surface_roots_) {
        if (entry.root == root) { return entry.surface; }
    }
    return nullptr;
}

base::Result<float> Context::scale(ElementId element) const {
    if (!alive(element)) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "DPI scale requires a live GUI element");
    }
    if (auto* surface = surface_of(element); surface != nullptr) {
        return root_ == surface->root_ ? scale_ : surface->scale_;
    }
    // Surface dispatch temporarily parks the primary scale in the active view.
    if (auto* active = surface_of(root_); active != nullptr) {
        return active->scale_;
    }
    return scale_;
}

base::Status Context::run_element_update(ElementId element, const Updater& update) {
    if (auto* surface = surface_of(element); surface != nullptr && root_ != surface->root()) {
        return surface->update(update);
    }
    return update();
}

void Surface::assemble(const DrawOptions& options, DrawData& out) {
    if (!alive()) { out = {}; return; }
    Scope scope(*this);
    gui::assemble(*context_, options, out);
}

base::Result<std::vector<renderer::specialist::RenderBatch>> specialist::SurfaceInterop::record(
    Surface& surface, Painter& painter, std::uint32_t slot, rhi::Extent2D target) {
    if (!surface.alive()) { return invalid_surface(); }
    Surface::Scope scope(surface);
    return painter.record(*surface.context_, slot, target);
}

} // namespace truffle::gui
