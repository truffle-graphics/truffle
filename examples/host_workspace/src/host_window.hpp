#pragma once

#include "truffle/core/status.hpp"
#include "truffle/rhi/rhi.hpp"

#include <string_view>

struct GLFWwindow;

namespace truffle::examples::host_workspace {

class HostWindow {
public:
    HostWindow() = default;
    ~HostWindow();

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    [[nodiscard]] core::Status open(std::string_view title, rhi::Extent2D extent);
    [[nodiscard]] bool should_close() const noexcept;
    [[nodiscard]] rhi::Extent2D framebuffer_extent() const noexcept;
    [[nodiscard]] void* native_handle() const noexcept;
    // On macOS: returns the CAMetalLayer* attached to the window's content view.
    // On other platforms: returns nullptr.
    [[nodiscard]] void* native_layer_handle() const noexcept;
    void poll_events() const noexcept;

private:
    GLFWwindow* window_ = nullptr;
    bool glfwInitialized_ = false;
};

} // namespace truffle::examples::host_workspace
