#include "host_window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>

namespace truffle::examples::host_workspace {
namespace {

[[nodiscard]] core::Status glfw_failure(std::string_view context) {
    const char* description = nullptr;
    glfwGetError(&description);

    std::string message{context};
    if (description != nullptr) {
        message += ": ";
        message += description;
    }
    return core::Status::failure(core::StatusCode::backend_error,
                                 std::move(message));
}

} // namespace

HostWindow::~HostWindow() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    if (glfwInitialized_) {
        glfwTerminate();
    }
}

core::Status HostWindow::open(std::string_view title, rhi::Extent2D extent) {
    if (window_ != nullptr || glfwInitialized_) {
        return core::Status::failure(core::StatusCode::invalid_state,
                                     "host window is already open");
    }
    if (extent.width == 0 || extent.height == 0) {
        return core::Status::failure(core::StatusCode::invalid_argument,
                                     "host window extent must be non-zero");
    }
    if (glfwInit() != GLFW_TRUE) {
        return glfw_failure("initialize GLFW");
    }
    glfwInitialized_ = true;

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(static_cast<int>(extent.width),
                               static_cast<int>(extent.height),
                               std::string{title}.c_str(), nullptr, nullptr);
    if (window_ == nullptr) {
        const auto status = glfw_failure("create GLFW host window");
        glfwTerminate();
        glfwInitialized_ = false;
        return status;
    }

    return core::Status::success();
}

bool HostWindow::should_close() const noexcept {
    return window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE;
}

rhi::Extent2D HostWindow::framebuffer_extent() const noexcept {
    if (window_ == nullptr) {
        return rhi::Extent2D{0, 0};
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return rhi::Extent2D{0, 0};
    }
    return rhi::Extent2D{static_cast<std::uint32_t>(width),
                         static_cast<std::uint32_t>(height)};
}

void* HostWindow::native_handle() const noexcept {
    return window_;
}

#ifndef __APPLE__
void* HostWindow::native_layer_handle() const noexcept {
    return nullptr;
}
#endif

void HostWindow::poll_events() const noexcept {
    glfwPollEvents();
}

} // namespace truffle::examples::host_workspace
