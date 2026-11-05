#pragma once

#include "truffle/gui/painter.hpp"

#include <memory>

namespace truffle::gui {

// An independent logical view of the same live Context, not a second tree or
// a platform window. The host owns its native window, presentation and routing.
// Context must outlive Surface. Call Context::prepare_frame once for all views,
// then assemble each view separately. Scheduling/invalidation remain shared.
// Events and element updaters see this view's root, input and native Host.
// Single-thread owned; destroy only after its calls/callbacks have returned.
class Surface {
public:
    [[nodiscard]] static base::Result<std::unique_ptr<Surface>> create(Context& context);
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    ~Surface();

    [[nodiscard]] Context& context() const noexcept { return *context_; }
    [[nodiscard]] ElementId root() const noexcept { return root_; }
    [[nodiscard]] bool alive() const noexcept;
    [[nodiscard]] base::Status set_viewport(Size size, float scale = 1.0F);
    void set_host(Host* host) noexcept;
    // Clears input ownership before transferring the actual live subtree.
    [[nodiscard]] base::Status attach(ElementId content);
    [[nodiscard]] base::Status release_input();
    [[nodiscard]] base::Status focus(ElementId element);
    [[nodiscard]] base::Status handle(const InputEvent& event);
    void assemble(const DrawOptions& options, DrawData& out);
    [[nodiscard]] base::Result<std::vector<renderer::RenderBatch>> record(
        Painter& painter, std::uint32_t slot, rhi::Extent2D target);

private:
    friend class Context;
    explicit Surface(Context& context) : context_(&context) {}
    class Scope;
    base::Status update(const Context::Updater& updater);
    base::Status validate_input();
    base::Status remove(ElementId element);
    Context* context_;
    ElementId root_;
    Host* host_ = nullptr;
    float scale_ = 1.0F;
    InputState input_;
    ElementId hovered_;
    ElementId focused_;
    std::unordered_map<std::uint32_t, ElementId> captures_;
    std::unordered_map<std::uint32_t, std::uint8_t> pointer_buttons_;
    base::Status event_status_;
};

} // namespace truffle::gui
