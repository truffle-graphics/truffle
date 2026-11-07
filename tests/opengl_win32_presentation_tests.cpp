#include "truffle/rhi/opengl_backend.hpp"
#include "gl_graphics_tests.hpp"
#include "opengl_backend_test.hpp"

#include <windows.h>
#include <crtdbg.h>
#include <cstdio>
#include <thread>

namespace {
namespace rhi = truffle::rhi;

class TestWindow {
public:
    TestWindow(std::uint32_t width, std::uint32_t height, bool ownDC = false)
        : className(ownDC ? L"TruffleWGLOwnedDCProof" : L"TruffleWGLHostProof") {
        const WNDCLASSW type{
            .style = ownDC ? UINT{CS_OWNDC} : UINT{0},
            .lpfnWndProc = DefWindowProcW,
            .hInstance = GetModuleHandleW(nullptr),
            .lpszClassName = className,
        };
        atom = RegisterClassW(&type);
        assert(atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        window = CreateWindowExW(0, className, L"Truffle WGL native proof",
                                 WS_POPUP, 80, 80, static_cast<int>(width),
                                 static_cast<int>(height), nullptr, nullptr,
                                 type.hInstance, nullptr);
        assert(window != nullptr);
        ShowWindow(window, SW_SHOW);
        pump();
    }
    ~TestWindow() {
        destroy();
        if (atom != 0) {
            assert(UnregisterClassW(className, GetModuleHandleW(nullptr)));
        }
    }
    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;
    void resize(std::uint32_t width, std::uint32_t height) {
        assert(SetWindowPos(window, nullptr, 0, 0, static_cast<int>(width),
                            static_cast<int>(height), SWP_NOMOVE | SWP_NOZORDER));
        pump();
        RECT rectangle{};
        assert(GetClientRect(window, &rectangle));
        assert(rectangle.right == static_cast<LONG>(width));
        assert(rectangle.bottom == static_cast<LONG>(height));
    }
    void show(int state) { ShowWindow(window, state); pump(); }
    void destroy() {
        if (window != nullptr) {
            assert(DestroyWindow(window));
            window = nullptr;
            pump();
        }
    }
    static void pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    HWND window = nullptr;
private:
    const wchar_t* className;
    ATOM atom = 0;
};

struct HostContext {
    explicit HostContext(HWND windowValue) : window(windowValue), dc(GetDC(window)) {
        assert(dc != nullptr);
        PIXELFORMATDESCRIPTOR descriptor{};
        descriptor.nSize = sizeof(descriptor);
        descriptor.nVersion = 1;
        descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        descriptor.iPixelType = PFD_TYPE_RGBA;
        descriptor.cColorBits = 24;
        descriptor.cAlphaBits = 8;
        const auto format = ChoosePixelFormat(dc, &descriptor);
        assert(format != 0 && SetPixelFormat(dc, format, &descriptor));
        context = wglCreateContext(dc);
        assert(context != nullptr && wglMakeCurrent(dc, context));
    }
    ~HostContext() {
        assert(wglMakeCurrent(nullptr, nullptr));
        assert(wglDeleteContext(context));
        ReleaseDC(window, dc);
    }
    void verify() const {
        assert(wglGetCurrentContext() == context);
        assert(wglGetCurrentDC() == dc);
    }
    HWND window;
    HDC dc;
    HGLRC context = nullptr;
};

void verify_pixels(rhi::Extent2D extent, bool split) {
    const auto capture = rhi::detail::opengl_last_present_capture_for_testing();
    assert(capture.extent == extent);
    assert(capture.rgba.size() == static_cast<std::size_t>(extent.width) * extent.height * 4);
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        const std::array<std::uint8_t, 4> expected =
            split && y < extent.height / 2
                ? std::array<std::uint8_t, 4>{0, 255, 0, 255}
                : std::array<std::uint8_t, 4>{0, 0, 255, 255};
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * extent.width + x) * 4;
            assert(std::equal(expected.begin(), expected.end(),
                              capture.rgba.begin() + offset));
        }
    }
}

rhi::StatusCode frame(rhi::Queue& queue, rhi::CommandPool& pool,
                      rhi::Swapchain& swapchain, rhi::Pipeline& pipeline,
                      rhi::StatusCode expectedAcquire, bool split = true) {
    auto acquired = swapchain.acquire_next_image();
    assert(acquired.ok() && acquired.status.code == expectedAcquire);
    const auto extent = acquired.image->desc().extent;
    auto list = pool.allocate();
    assert(list.ok() && list.value().begin().ok());
    rhi::BarrierBatch toRender;
    toRender.textures.push_back({
        .texture = acquired.image,
        .oldLayout = rhi::TextureLayout::present,
        .newLayout = rhi::TextureLayout::color_attachment,
        .sourceStages = rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::color_attachment_output,
        .destinationAccess = rhi::Access::color_attachment_write});
    assert(list.value().barrier(toRender).ok());
    auto render = list.value().begin_rendering({
        .extent = {extent.width, extent.height},
        .colorAttachments = {{.texture = acquired.image,
                              .clear = {0, 0, 1, 1}}},
    });
    assert(render.ok());
    if (split) {
        assert(render.value().bind_pipeline(pipeline).ok());
        const std::array viewports{rhi::Viewport{
            0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1}};
        const std::array scissors{rhi::ScissorRect{0, 0, extent.width, extent.height / 2}};
        assert(render.value().set_viewports(0, viewports).ok());
        assert(render.value().set_scissors(0, scissors).ok());
        assert(render.value().draw(3).ok());
    }
    assert(render.value().end().ok());
    rhi::BarrierBatch toPresent;
    toPresent.textures.push_back({
        .texture = acquired.image,
        .oldLayout = rhi::TextureLayout::color_attachment,
        .newLayout = rhi::TextureLayout::present,
        .sourceStages = rhi::PipelineStage::color_attachment_output,
        .destinationStages = rhi::PipelineStage::bottom,
        .sourceAccess = rhi::Access::color_attachment_write});
    assert(list.value().barrier(toPresent).ok());
    assert(list.value().end().ok());
    std::array<rhi::CommandList*, 1> lists{&list.value()};
    const std::array<rhi::SemaphoreWait, 1> waits{{
        {.semaphore = acquired.available, .value = acquired.availableValue},
    }};
    assert(queue.submit({.commandLists = lists, .waits = waits}).ok());
    return queue.present(swapchain, acquired.imageIndex).code;
}
} // namespace

int main() {
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    TestWindow hostWindow{16, 16, true};
    HostContext host{hostWindow.window};
    std::size_t diagnostics = 0;
    auto instance = rhi::create_opengl_instance({
        .enableValidation = true,
        .debugCallback = &truffle::tests::count_native_diagnostic,
        .debugUserData = &diagnostics,
    });
    assert(instance.ok());
    host.verify();
    auto adapter = instance.value().adapter(0);
    assert(adapter.ok() && adapter.value().info().presentation);
    assert(adapter.value().info().name.find("[KHR_debug]") != std::string::npos);
    auto device = adapter.value().request_device({
        .requiredFeatures = {rhi::Feature::presentation}});
    assert(device.ok());
    auto queue = device.value().queue(rhi::QueueKind::graphics);
    auto pool = device.value().create_command_pool(rhi::QueueKind::graphics);
    assert(queue.ok() && pool.ok());
    auto vertex = truffle::tests::gl_fixture_shader(device.value(), true,
        rhi::ShaderStage::vertex, R"(
const vec2 positions[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main() { gl_Position = vec4(positions[gl_VertexID],0,1); }
)");
    auto fragment = truffle::tests::gl_fixture_shader(device.value(), true,
        rhi::ShaderStage::fragment, R"(
layout(location=0) out vec4 outputColor;
void main() { outputColor = vec4(0,1,0,1); }
)");
    auto pipeline = device.value().create_pipeline({
        .vertexShader = &vertex, .fragmentShader = &fragment,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
        .dynamicState = rhi::DynamicState::viewport | rhi::DynamicState::scissor});
    assert(pipeline.ok());
    TestWindow window{96, 64};
    auto surface = device.value().create_surface({
        .native = {.kind = rhi::NativeSurfaceKind::win32, .handle = window.window},
        .initialExtent = {96, 64}});
    assert(surface.ok());
    assert(device.value().create_surface({
        .native = {.kind = rhi::NativeSurfaceKind::win32}}).status().code ==
        rhi::StatusCode::invalid_argument);
    auto mailbox = device.value().create_swapchain(surface.value(), {
        .extent = {96, 64}, .presentMode = rhi::PresentMode::mailbox});
    assert(mailbox.status().code == rhi::StatusCode::unsupported);
    auto swapchain = device.value().create_swapchain(surface.value(), {
        .extent = {96, 64}, .format = rhi::TextureFormat::rgba8_unorm});
    assert(swapchain.ok());
    rhi::detail::set_opengl_present_capture_for_testing(true);
    for (int index = 0; index < 3; ++index) {
        assert(frame(queue.value(), pool.value(), swapchain.value(), pipeline.value(),
                     rhi::StatusCode::ok) == rhi::StatusCode::ok);
        verify_pixels({96, 64}, true);
        host.verify();
    }
    rhi::StatusCode offThread = rhi::StatusCode::ok;
    std::thread([&] {
        offThread = swapchain.value().acquire_next_image().status.code;
    }).join();
    assert(offThread == rhi::StatusCode::invalid_state);
    {
        auto retry = swapchain.value().acquire_next_image();
        assert(retry.ok());
        std::thread([&] {
            offThread = queue.value().present(swapchain.value(), retry.imageIndex).code;
        }).join();
        assert(offThread == rhi::StatusCode::invalid_state);
        assert(queue.value().present(swapchain.value(), retry.imageIndex).ok());
        verify_pixels({96, 64}, true);
        host.verify();
    }
    std::thread([&] {
        offThread = device.value().create_surface({
            .native = {.kind = rhi::NativeSurfaceKind::win32,
                       .handle = window.window}}).status().code;
    }).join();
    assert(offThread == rhi::StatusCode::invalid_state);
    window.resize(128, 80);
    assert(frame(queue.value(), pool.value(), swapchain.value(), pipeline.value(),
                 rhi::StatusCode::suboptimal) == rhi::StatusCode::suboptimal);
    assert(swapchain.value().resize({128, 80}).ok());
    assert(frame(queue.value(), pool.value(), swapchain.value(), pipeline.value(),
                 rhi::StatusCode::ok) == rhi::StatusCode::ok);
    verify_pixels({128, 80}, true);
    window.show(SW_MINIMIZE);
    assert(swapchain.value().acquire_next_image().status.code == rhi::StatusCode::out_of_date);
    window.show(SW_RESTORE);
    window.resize(0, 0);
    assert(swapchain.value().acquire_next_image().status.code == rhi::StatusCode::out_of_date);
    window.resize(128, 80);
    assert(swapchain.value().resize({128, 80}).ok());
    assert(frame(queue.value(), pool.value(), swapchain.value(), pipeline.value(),
                 rhi::StatusCode::ok) == rhi::StatusCode::ok);
    verify_pixels({128, 80}, true);
    auto acquired = swapchain.value().acquire_next_image();
    assert(acquired.ok());
    window.destroy();
    assert(queue.value().present(swapchain.value(), acquired.imageIndex).code ==
           rhi::StatusCode::surface_lost);
    assert(!device.value().lost());
    TestWindow replacement{64, 48};
    auto replacementSurface = device.value().create_surface({
        .native = {.kind = rhi::NativeSurfaceKind::win32, .handle = replacement.window},
        .initialExtent = {64, 48}});
    assert(replacementSurface.ok());
    auto replacementSwapchain = device.value().create_swapchain(replacementSurface.value(), {
        .extent = {64, 48}, .format = rhi::TextureFormat::bgra8_unorm,
        .presentMode = rhi::PresentMode::immediate});
    assert(replacementSwapchain.ok());
    assert(frame(queue.value(), pool.value(), replacementSwapchain.value(), pipeline.value(),
                 rhi::StatusCode::ok, false) == rhi::StatusCode::ok);
    verify_pixels({64, 48}, false);
    replacement.destroy();
    assert(replacementSwapchain.value().acquire_next_image().status.code ==
           rhi::StatusCode::surface_lost);
    host.verify();
    std::thread([owned = std::move(replacementSwapchain).value()]() mutable {
        { auto retiring = std::move(owned); }
        assert(wglGetCurrentContext() == nullptr);
    }).join();
    auto workerOwned = rhi::create_opengl_instance({.enableValidation = true});
    assert(workerOwned.ok());
    host.verify();
    std::thread([owned = std::move(workerOwned).value()]() mutable {
        {
            auto retiring = std::move(owned);
            auto workerAdapter = retiring.adapter(0);
            assert(workerAdapter.ok());
            auto workerDevice = workerAdapter.value().request_device();
            assert(workerDevice.ok());
            auto buffer = workerDevice.value().create_buffer({
                .size = 32, .usage = rhi::BufferUsage::copy_destination,
                .memory = rhi::MemoryDomain::readback});
            assert(buffer.ok());
        }
        assert(wglGetCurrentContext() == nullptr);
    }).join();
    auto lostList = pool.value().allocate();
    assert(lostList.ok() && lostList.value().begin().ok() && lostList.value().end().ok());
    std::array<rhi::CommandList*, 1> lostLists{&lostList.value()};
    rhi::detail::set_opengl_context_loss_for_testing(true);
    assert(queue.value().submit(lostLists).code == rhi::StatusCode::device_lost);
    assert(device.value().lost());
    rhi::detail::set_opengl_context_loss_for_testing(false);
    auto recovered = rhi::create_opengl_instance({.enableValidation = true});
    assert(recovered.ok());
    host.verify();
    rhi::detail::set_opengl_present_capture_for_testing(false);
    assert(diagnostics == 0);
    std::puts("WGL exact window pixels, resize/recovery and caller-context restoration passed");
    return 0;
}
