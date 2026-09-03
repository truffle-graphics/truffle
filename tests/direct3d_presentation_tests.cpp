#include "truffle/rhi/direct3d_backend.hpp"

#include "direct3d_backend_test.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace rhi = truffle::rhi;

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

class TestWindow {
public:
  TestWindow(std::uint32_t width, std::uint32_t height) {
    instance_ = GetModuleHandleW(nullptr);
    const WNDCLASSW windowClass{
        .lpfnWndProc = &window_procedure,
        .hInstance = instance_,
        .lpszClassName = className_,
    };
    atom_ = RegisterClassW(&windowClass);
    assert(atom_ != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    assert(AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE));
    window_ = CreateWindowExW(
        0, className_, L"Truffle D3D12 presentation proof", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, nullptr);
    assert(window_ != nullptr);
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    pump_messages();
  }

  ~TestWindow() {
    destroy();
    if (atom_ != 0) {
      UnregisterClassW(className_, instance_);
    }
  }

  TestWindow(const TestWindow &) = delete;
  TestWindow &operator=(const TestWindow &) = delete;

  [[nodiscard]] HWND get() const noexcept { return window_; }

  void resize(std::uint32_t width, std::uint32_t height) {
    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    assert(AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE));
    assert(SetWindowPos(window_, nullptr, 0, 0, rectangle.right - rectangle.left,
                        rectangle.bottom - rectangle.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    pump_messages();
  }

  void destroy() {
    if (window_ != nullptr) {
      DestroyWindow(window_);
      window_ = nullptr;
      pump_messages();
    }
  }

private:
  static void pump_messages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  static constexpr const wchar_t *className_ = L"TruffleD3D12PresentationProof";
  HINSTANCE instance_ = nullptr;
  ATOM atom_ = 0;
  HWND window_ = nullptr;
};

struct PresentationContext {
  rhi::Instance instance;
  rhi::Adapter adapter;
  rhi::Device device;
  rhi::Queue queue;
  rhi::CommandPool pool;
};

PresentationContext create_context() {
  auto instance = rhi::create_direct3d12_instance({.enableValidation = true});
  assert(instance.ok());
  auto adapter = instance.value().adapter(0);
  assert(adapter.ok());
  assert(adapter.value().info().native);
  assert(adapter.value().info().maturity == rhi::BackendMaturity::native_smoke);
  assert(std::find(adapter.value().info().supportedFeatures.begin(),
                   adapter.value().info().supportedFeatures.end(),
                   rhi::Feature::presentation) !=
         adapter.value().info().supportedFeatures.end());
  auto device = adapter.value().request_device({
      .requiredFeatures = {rhi::Feature::presentation, rhi::Feature::transfer},
  });
  assert(device.ok());
  auto queue = device.value().queue(rhi::QueueKind::graphics);
  auto pool = device.value().create_command_pool(rhi::QueueKind::graphics);
  assert(queue.ok() && pool.ok());
  return {
      .instance = std::move(instance).value(),
      .adapter = std::move(adapter).value(),
      .device = std::move(device).value(),
      .queue = std::move(queue).value(),
      .pool = std::move(pool).value(),
  };
}

rhi::Surface create_surface(PresentationContext &context, HWND window,
                            rhi::Extent2D extent) {
  auto surface = context.device.create_surface({
      .native = {.kind = rhi::NativeSurfaceKind::win32, .handle = window},
      .initialExtent = extent,
      .debugName = "D3D12 Win32 test surface",
  });
  assert(surface.ok());
  return std::move(surface).value();
}

rhi::Swapchain create_swapchain(PresentationContext &context, rhi::Surface &surface,
                                rhi::Extent2D extent) {
  auto swapchain = context.device.create_swapchain(
      surface, {.extent = extent,
                .format = rhi::TextureFormat::bgra8_unorm,
                .presentMode = rhi::PresentMode::fifo,
                .imageCount = 2,
                .debugName = "D3D12 DXGI test swapchain"});
  assert(swapchain.ok());
  return std::move(swapchain).value();
}

void render_readback_and_present(PresentationContext &context, rhi::Swapchain &swapchain,
                                 rhi::Extent2D extent) {
  constexpr std::size_t rowPitch = 256;
  auto acquired = swapchain.acquire_next_image();
  assert(acquired.ok());
  assert(acquired.image->desc().extent.width == extent.width);
  assert(acquired.image->desc().extent.height == extent.height);
  auto readback = context.device.create_buffer({
      .size = rowPitch * extent.height,
      .usage = rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::readback,
  });
  auto rendered = context.device.create_semaphore();
  auto list = context.pool.allocate();
  assert(readback.ok() && rendered.ok() && list.ok());
  assert(list.value().begin().ok());

  rhi::BarrierBatch toRender;
  toRender.textures.push_back({
      .texture = acquired.image,
      .oldLayout = rhi::TextureLayout::present,
      .newLayout = rhi::TextureLayout::color_attachment,
      .sourceStages = rhi::PipelineStage::top,
      .destinationStages = rhi::PipelineStage::color_attachment_output,
      .destinationAccess = rhi::Access::color_attachment_write,
  });
  assert(list.value().barrier(toRender).ok());
  auto render = list.value().begin_rendering({
      .extent = extent,
      .colorAttachments = {{.texture = acquired.image,
                            .clear = {0.25F, 0.5F, 0.75F, 1.0F}}},
  });
  assert(render.ok());
  assert(render.value().end().ok());

  rhi::BarrierBatch toCopy;
  toCopy.textures.push_back({
      .texture = acquired.image,
      .oldLayout = rhi::TextureLayout::color_attachment,
      .newLayout = rhi::TextureLayout::transfer_source,
      .sourceStages = rhi::PipelineStage::color_attachment_output,
      .destinationStages = rhi::PipelineStage::copy,
      .sourceAccess = rhi::Access::color_attachment_write,
      .destinationAccess = rhi::Access::transfer_read,
  });
  assert(list.value().barrier(toCopy).ok());
  auto copy = list.value().begin_copy();
  assert(copy.ok());
  assert(copy.value()
             .copy_texture_to_buffer(
                 *acquired.image, readback.value(),
                 {.layout = {.bytesPerRow = rowPitch, .rowsPerImage = extent.height},
                  .texture = {.subresource = {.aspect = rhi::TextureAspect::color},
                              .extent = {extent.width, extent.height, 1}}})
             .ok());
  assert(copy.value().end().ok());
  rhi::BarrierBatch toPresent;
  toPresent.textures.push_back({
      .texture = acquired.image,
      .oldLayout = rhi::TextureLayout::transfer_source,
      .newLayout = rhi::TextureLayout::present,
      .sourceStages = rhi::PipelineStage::copy,
      .destinationStages = rhi::PipelineStage::bottom,
      .sourceAccess = rhi::Access::transfer_read,
  });
  assert(list.value().barrier(toPresent).ok());
  assert(list.value().end().ok());

  std::array<rhi::CommandList *, 1> lists{&list.value()};
  const std::array<rhi::SemaphoreWait, 1> acquireWaits{{
      {.semaphore = acquired.available,
       .value = acquired.availableValue,
       .stages = rhi::PipelineStage::color_attachment_output},
  }};
  const std::array<rhi::SemaphoreSignal, 1> renderSignals{{
      {.semaphore = &rendered.value(), .value = 1},
  }};
  assert(context.queue
             .submit(
                 {.commandLists = lists, .waits = acquireWaits, .signals = renderSignals})
             .ok());

  std::vector<std::byte> pixels(rowPitch * extent.height);
  assert(readback.value().read(0, pixels).ok());
  const std::array expected{std::byte{191}, std::byte{128}, std::byte{64},
                            std::byte{255}};
  for (std::uint32_t row = 0; row < extent.height; ++row) {
    for (std::uint32_t column = 0; column < extent.width; ++column) {
      const auto offset = row * rowPitch + column * expected.size();
      assert(std::equal(expected.begin(), expected.end(), pixels.begin() + offset));
    }
  }
  const std::array<rhi::SemaphoreWait, 1> presentWaits{{
      {.semaphore = &rendered.value(), .value = 1, .stages = rhi::PipelineStage::bottom},
  }};
  assert(context.queue
             .present({.swapchain = &swapchain,
                       .imageIndex = acquired.imageIndex,
                       .waits = presentWaits})
             .ok());
}

void verify_status_paths(PresentationContext &context, rhi::Swapchain &swapchain) {
  using rhi::detail::Direct3DAcquireFault;
  using rhi::detail::Direct3DPresentFault;

  for (const auto [fault, code] : std::array{
           std::pair{Direct3DAcquireFault::timeout, rhi::StatusCode::timeout},
           std::pair{Direct3DAcquireFault::out_of_date, rhi::StatusCode::out_of_date},
           std::pair{Direct3DAcquireFault::surface_lost, rhi::StatusCode::surface_lost},
           std::pair{Direct3DAcquireFault::out_of_memory,
                     rhi::StatusCode::out_of_memory}}) {
    rhi::detail::set_direct3d_acquire_fault_for_testing(fault);
    assert(swapchain.acquire_next_image().status.code == code);
  }
  rhi::detail::set_direct3d_acquire_fault_for_testing(Direct3DAcquireFault::suboptimal);
  auto suboptimalAcquire = swapchain.acquire_next_image();
  assert(suboptimalAcquire.ok());
  assert(suboptimalAcquire.status.code == rhi::StatusCode::suboptimal);
  rhi::detail::set_direct3d_acquire_fault_for_testing(Direct3DAcquireFault::none);
  assert(context.queue.present(swapchain, suboptimalAcquire.imageIndex).ok());

  for (const auto [fault, code] : std::array{
           std::pair{Direct3DPresentFault::timeout, rhi::StatusCode::timeout},
           std::pair{Direct3DPresentFault::surface_lost, rhi::StatusCode::surface_lost},
           std::pair{Direct3DPresentFault::out_of_memory,
                     rhi::StatusCode::out_of_memory}}) {
    auto acquired = swapchain.acquire_next_image();
    assert(acquired.ok());
    rhi::detail::set_direct3d_present_fault_for_testing(fault);
    assert(context.queue.present(swapchain, acquired.imageIndex).code == code);
    rhi::detail::set_direct3d_present_fault_for_testing(Direct3DPresentFault::none);
    assert(context.queue.present(swapchain, acquired.imageIndex).ok());
  }

  auto outOfDateImage = swapchain.acquire_next_image();
  assert(outOfDateImage.ok());
  rhi::detail::set_direct3d_present_fault_for_testing(Direct3DPresentFault::out_of_date);
  assert(context.queue.present(swapchain, outOfDateImage.imageIndex).code ==
         rhi::StatusCode::out_of_date);
  rhi::detail::set_direct3d_present_fault_for_testing(Direct3DPresentFault::none);

  auto suboptimalImage = swapchain.acquire_next_image();
  assert(suboptimalImage.ok());
  rhi::detail::set_direct3d_present_fault_for_testing(Direct3DPresentFault::suboptimal);
  assert(context.queue.present(swapchain, suboptimalImage.imageIndex).code ==
         rhi::StatusCode::suboptimal);
  rhi::detail::set_direct3d_present_fault_for_testing(Direct3DPresentFault::none);
}

} // namespace

int main() {
  auto context = create_context();
  TestWindow window{64, 48};

  auto invalidSurface = context.device.create_surface({
      .native = {.kind = rhi::NativeSurfaceKind::headless},
      .initialExtent = {64, 48},
  });
  assert(invalidSurface.status().code == rhi::StatusCode::unsupported);

  auto surface = create_surface(context, window.get(), {64, 48});
  auto mailbox = context.device.create_swapchain(
      surface,
      {.extent = {64, 48}, .presentMode = rhi::PresentMode::mailbox, .imageCount = 2});
  assert(mailbox.status().code == rhi::StatusCode::unsupported);
  auto swapchain = create_swapchain(context, surface, {64, 48});
  render_readback_and_present(context, swapchain, {64, 48});

  window.resize(80, 60);
  auto drifted = swapchain.acquire_next_image();
  assert(drifted.ok());
  assert(drifted.status.code == rhi::StatusCode::suboptimal);
  assert(context.queue.present(swapchain, drifted.imageIndex).code ==
         rhi::StatusCode::suboptimal);
  assert(swapchain.resize({80, 60}).ok());
  render_readback_and_present(context, swapchain, {80, 60});

  verify_status_paths(context, swapchain);

  TestWindow doomedWindow{32, 24};
  auto doomedSurface = create_surface(context, doomedWindow.get(), {32, 24});
  auto doomedSwapchain = create_swapchain(context, doomedSurface, {32, 24});
  doomedWindow.destroy();
  assert(doomedSwapchain.acquire_next_image().status.code ==
         rhi::StatusCode::surface_lost);

  auto lostImage = swapchain.acquire_next_image();
  assert(lostImage.ok());
  rhi::detail::set_direct3d_present_fault_for_testing(
      rhi::detail::Direct3DPresentFault::device_lost);
  assert(context.queue.present(swapchain, lostImage.imageIndex).code ==
         rhi::StatusCode::device_lost);
  assert(context.device.lost());
  rhi::detail::set_direct3d_present_fault_for_testing(
      rhi::detail::Direct3DPresentFault::none);

  auto recoveredDevice = context.adapter.request_device({
      .requiredFeatures = {rhi::Feature::presentation},
  });
  assert(recoveredDevice.ok());
  assert(!recoveredDevice.value().lost());
  return 0;
}
