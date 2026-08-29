#include "truffle/rhi/direct3d_backend.hpp"

#include "foundation_backend.hpp"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#endif

namespace truffle::rhi {

#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] Status direct3d_failure(StatusCode code, std::string message,
                                      HRESULT result) {
    BackendDiagnostic detail{
        .domain = "d3d12",
        .nativeCode = static_cast<std::int64_t>(result),
        .objectLabel = {},
        .message = message,
    };
    return Status::failure(code, std::move(message), std::move(detail));
}

[[nodiscard]] std::string utf8_name(const wchar_t* value) {
    if (value == nullptr || value[0] == L'\0') {
        return "Direct3D 12 WARP adapter";
    }
    const auto length = static_cast<int>(std::wcslen(value));
    const auto required = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr,
                                               0, nullptr, nullptr);
    if (required <= 0) {
        return "Direct3D 12 WARP adapter";
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), required,
                        nullptr, nullptr);
    return result;
}

struct Direct3DContext {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    std::uint64_t fenceValue = 0;
    std::mutex mutex;
};

[[nodiscard]] Status wait_for_fence(Direct3DContext& context,
                                    std::uint64_t value) {
    if (context.fence->GetCompletedValue() >= value) {
        return Status::success();
    }
    const auto eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 fence event creation failed",
                                HRESULT_FROM_WIN32(GetLastError()));
    }
    const auto result = context.fence->SetEventOnCompletion(value, eventHandle);
    if (FAILED(result)) {
        CloseHandle(eventHandle);
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 fence wait setup failed", result);
    }
    const auto waitResult = WaitForSingleObject(eventHandle, 30000);
    CloseHandle(eventHandle);
    if (waitResult != WAIT_OBJECT_0) {
        return Status::failure(StatusCode::timeout,
                               "D3D12 WARP fence wait timed out");
    }
    return Status::success();
}

[[nodiscard]] Status submit_empty(Direct3DContext& context) {
    ++context.fenceValue;
    const auto result = context.queue->Signal(context.fence.Get(),
                                               context.fenceValue);
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 WARP queue signal failed", result);
    }
    return wait_for_fence(context, context.fenceValue);
}

struct Direct3DProbe {
    std::shared_ptr<Direct3DContext> context;
    std::string adapterName;
    std::size_t deviceLocalBudget = 1024u * 1024u * 1024u;
};

[[nodiscard]] Result<Direct3DProbe> initialize_direct3d(
    const InstanceDesc& desc) {
    UINT factoryFlags = 0;
    if (desc.enableValidation) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    ComPtr<IDXGIFactory6> factory;
    auto result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));
    if (FAILED(result) && factoryFlags != 0) {
        factoryFlags = 0;
        result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));
    }
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::unavailable,
                                "DXGI factory creation failed", result);
    }
    ComPtr<IDXGIAdapter1> adapter;
    result = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::unavailable,
                                "DXGI did not expose the WARP adapter", result);
    }

    auto context = std::make_shared<Direct3DContext>();
    result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&context->device));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::unavailable,
                                "D3D12 WARP device creation failed", result);
    }
    const D3D12_COMMAND_QUEUE_DESC queueDesc{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };
    result = context->device->CreateCommandQueue(
        &queueDesc, IID_PPV_ARGS(&context->queue));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 WARP command-queue creation failed",
                                result);
    }
    result = context->device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(&context->fence));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 WARP fence creation failed", result);
    }
    ComPtr<ID3D12CommandAllocator> allocator;
    result = context->device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 WARP command-allocator creation failed",
                                result);
    }
    ComPtr<ID3D12GraphicsCommandList> commandList;
    result = context->device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&commandList));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 WARP command-list creation failed",
                                result);
    }
    result = commandList->Close();
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 WARP command-list close failed", result);
    }
    ID3D12CommandList* commandLists[] = {commandList.Get()};
    context->queue->ExecuteCommandLists(1, commandLists);
    if (auto status = submit_empty(*context); !status.ok()) {
        return status;
    }

    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter->GetDesc1(&adapterDesc);
    return Direct3DProbe{
        .context = std::move(context),
        .adapterName = utf8_name(adapterDesc.Description),
        .deviceLocalBudget = adapterDesc.DedicatedVideoMemory != 0
                                 ? adapterDesc.DedicatedVideoMemory
                                 : 1024u * 1024u * 1024u,
    };
}

[[nodiscard]] Status submit_direct3d_commands(
    const std::shared_ptr<void>& nativeContext,
    std::span<const detail::NativeCommand> commands,
    std::span<const detail::NativeSemaphorePoint> waits,
    std::span<const detail::NativeSemaphorePoint> signals) {
    if (!commands.empty() || !waits.empty() || !signals.empty()) {
        return Status::failure(
            StatusCode::unsupported,
            "the D3D12 matrix slice currently supports empty native smoke submissions only");
    }
    const auto context =
        std::static_pointer_cast<Direct3DContext>(nativeContext);
    if (!context || !context->device || !context->queue || !context->fence) {
        return Status::failure(StatusCode::device_lost,
                               "the D3D12 native context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    return submit_empty(*context);
}

} // namespace
#endif

Result<Instance> create_direct3d12_instance(const InstanceDesc& desc) {
#ifdef _WIN32
    auto probe = initialize_direct3d(desc);
    if (!probe.ok()) {
        return probe.status();
    }
    auto native = std::move(probe).value();
    detail::FoundationBackendConfig config;
    config.kind = BackendKind::direct3d12;
    config.platform = PlatformKind::windows;
    config.maturity = BackendMaturity::native_smoke;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics};
    config.deviceLocalBudgetBytes = native.deviceLocalBudget;
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.nativeSubmit = &submit_direct3d_commands;
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::direct3d12, "Direct3D 12");
#endif
}

} // namespace truffle::rhi
