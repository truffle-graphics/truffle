#include "truffle/rhi/direct3d_backend.hpp"

#include "foundation_backend.hpp"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <cwchar>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
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

struct Direct3DBufferResource {
    std::shared_ptr<Direct3DContext> context;
    ComPtr<ID3D12Resource> resource;
    std::size_t size = 0;
    MemoryDomain memory = MemoryDomain::device_local;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    void* mapped = nullptr;
    std::mutex mutex;
};

[[nodiscard]] D3D12_HEAP_TYPE direct3d_heap_type(MemoryDomain memory) {
    switch (memory) {
    case MemoryDomain::upload:
        return D3D12_HEAP_TYPE_UPLOAD;
    case MemoryDomain::readback:
        return D3D12_HEAP_TYPE_READBACK;
    case MemoryDomain::device_local:
        return D3D12_HEAP_TYPE_DEFAULT;
    case MemoryDomain::external:
        break;
    }
    return D3D12_HEAP_TYPE_CUSTOM;
}

[[nodiscard]] D3D12_RESOURCE_STATES direct3d_initial_state(
    MemoryDomain memory) {
    switch (memory) {
    case MemoryDomain::upload:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case MemoryDomain::readback:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case MemoryDomain::device_local:
    case MemoryDomain::external:
        return D3D12_RESOURCE_STATE_COMMON;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

[[nodiscard]] Result<ComPtr<ID3D12Resource>> create_direct3d_buffer_resource(
    Direct3DContext& context, std::size_t size, MemoryDomain memory) {
    const D3D12_HEAP_PROPERTIES heapProperties{
        .Type = direct3d_heap_type(memory),
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 1,
        .VisibleNodeMask = 1,
    };
    const D3D12_RESOURCE_DESC resourceDesc{
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
    ComPtr<ID3D12Resource> resource;
    const auto result = context.device->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        direct3d_initial_state(memory), nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(result)) {
        return direct3d_failure(
            result == E_OUTOFMEMORY ? StatusCode::out_of_memory
                                    : StatusCode::backend_error,
            "D3D12 buffer allocation failed", result);
    }
    return resource;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_direct3d_buffer(
    const std::shared_ptr<void>& nativeContext, const BufferDesc& desc) {
    const auto context =
        std::static_pointer_cast<Direct3DContext>(nativeContext);
    if (!context || !context->device) {
        return Status::failure(StatusCode::device_lost,
                               "the D3D12 native context is unavailable");
    }
    if (desc.memory == MemoryDomain::external || desc.shareable) {
        return Status::failure(
            StatusCode::unsupported,
            "D3D12 external buffer memory is not implemented");
    }
    std::lock_guard contextLock{context->mutex};
    auto native =
        create_direct3d_buffer_resource(*context, desc.size, desc.memory);
    if (!native.ok()) {
        return native.status();
    }
    try {
        auto resource = std::make_shared<Direct3DBufferResource>();
        resource->context = context;
        resource->resource = std::move(native).value();
        resource->size = desc.size;
        resource->memory = desc.memory;
        resource->state = direct3d_initial_state(desc.memory);
        if (desc.mappedAtCreation) {
            const D3D12_RANGE readRange{0, 0};
            const auto result = resource->resource->Map(
                0, &readRange, &resource->mapped);
            if (FAILED(result)) {
                return direct3d_failure(StatusCode::backend_error,
                                        "D3D12 buffer mapping failed", result);
            }
        }
        return std::static_pointer_cast<void>(std::move(resource));
    } catch (const std::bad_alloc&) {
        return Status::failure(StatusCode::out_of_memory,
                               "D3D12 buffer resource allocation failed");
    }
}

[[nodiscard]] Status ensure_direct3d_buffer_mapped(
    Direct3DBufferResource& resource, bool forRead) {
    if (resource.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "default-heap D3D12 buffers are not host mappable");
    }
    if (resource.mapped != nullptr) {
        return Status::success();
    }
    const D3D12_RANGE readRange = forRead
                                      ? D3D12_RANGE{0, resource.size}
                                      : D3D12_RANGE{0, 0};
    const auto result = resource.resource->Map(0, &readRange,
                                                &resource.mapped);
    return SUCCEEDED(result)
               ? Status::success()
               : direct3d_failure(StatusCode::backend_error,
                                  "D3D12 buffer mapping failed", result);
}

[[nodiscard]] Result<std::span<std::byte>> map_direct3d_buffer(
    const std::shared_ptr<void>& nativeResource) {
    const auto resource =
        std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
    if (!resource) {
        return Status::failure(StatusCode::invalid_argument,
                               "D3D12 buffer resource is invalid");
    }
    std::lock_guard lock{resource->mutex};
    if (auto status = ensure_direct3d_buffer_mapped(
            *resource, resource->memory == MemoryDomain::readback);
        !status.ok()) {
        return status;
    }
    return std::span<std::byte>{static_cast<std::byte*>(resource->mapped),
                                resource->size};
}

[[nodiscard]] Status unmap_direct3d_buffer(
    const std::shared_ptr<void>& nativeResource) {
    const auto resource =
        std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
    if (!resource) {
        return Status::failure(StatusCode::invalid_argument,
                               "D3D12 buffer resource is invalid");
    }
    std::lock_guard lock{resource->mutex};
    if (resource->mapped == nullptr) {
        return Status::failure(StatusCode::invalid_state,
                               "D3D12 buffer is not mapped");
    }
    resource->resource->Unmap(0, nullptr);
    resource->mapped = nullptr;
    return Status::success();
}

[[nodiscard]] Status direct3d_buffer_range(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::size_t size) {
    const auto resource =
        std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
    return resource && offset <= resource->size && size <= resource->size - offset
               ? Status::success()
               : Status::failure(StatusCode::invalid_argument,
                                 "D3D12 buffer range is invalid");
}

[[nodiscard]] Status write_direct3d_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::span<const std::byte> data) {
    const auto resource =
        std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
    if (auto status = direct3d_buffer_range(nativeResource, offset, data.size());
        !status.ok()) {
        return status;
    }
    std::lock_guard lock{resource->mutex};
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_direct3d_buffer_mapped(*resource, false);
        !status.ok()) {
        return status;
    }
    std::memcpy(static_cast<std::byte*>(resource->mapped) + offset, data.data(),
                data.size());
    if (temporary) {
        const D3D12_RANGE writtenRange{offset, offset + data.size()};
        resource->resource->Unmap(0, &writtenRange);
        resource->mapped = nullptr;
    }
    return Status::success();
}

[[nodiscard]] Status read_direct3d_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::span<std::byte> data) {
    const auto resource =
        std::static_pointer_cast<Direct3DBufferResource>(nativeResource);
    if (auto status = direct3d_buffer_range(nativeResource, offset, data.size());
        !status.ok()) {
        return status;
    }
    std::lock_guard lock{resource->mutex};
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_direct3d_buffer_mapped(*resource, true);
        !status.ok()) {
        return status;
    }
    std::memcpy(data.data(),
                static_cast<const std::byte*>(resource->mapped) + offset,
                data.size());
    if (temporary) {
        const D3D12_RANGE writtenRange{0, 0};
        resource->resource->Unmap(0, &writtenRange);
        resource->mapped = nullptr;
    }
    return Status::success();
}

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

[[nodiscard]] bool direct3d_transfer_range_valid(
    const Direct3DBufferResource& resource, std::size_t offset,
    std::size_t size) {
    return size != 0 && offset <= resource.size &&
           size <= resource.size - offset;
}

[[nodiscard]] Status validate_direct3d_commands(
    std::span<const detail::NativeCommand> commands) {
    for (const auto& command : commands) {
        if (command.kind == detail::NativeCommandKind::barrier) {
            continue;
        }
        if (command.kind != detail::NativeCommandKind::transfer) {
            return Status::failure(
                StatusCode::unsupported,
                "the D3D12 resource slice supports transfer command lists only");
        }
        const auto& transfer = command.transfer;
        if (transfer.kind == detail::NativeTransferKind::copy_buffer) {
            const auto source =
                std::static_pointer_cast<Direct3DBufferResource>(
                    transfer.source);
            const auto destination =
                std::static_pointer_cast<Direct3DBufferResource>(
                    transfer.destination);
            if (!source || !destination ||
                !direct3d_transfer_range_valid(
                    *source, transfer.buffer.sourceOffset,
                    transfer.buffer.size) ||
                !direct3d_transfer_range_valid(
                    *destination, transfer.buffer.destinationOffset,
                    transfer.buffer.size)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "D3D12 buffer copy range is invalid");
            }
        } else if (transfer.kind == detail::NativeTransferKind::fill_buffer) {
            const auto destination =
                std::static_pointer_cast<Direct3DBufferResource>(
                    transfer.destination);
            if (!destination ||
                !direct3d_transfer_range_valid(
                    *destination, transfer.buffer.destinationOffset,
                    transfer.buffer.size)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "D3D12 buffer fill range is invalid");
            }
        } else {
            return Status::failure(
                StatusCode::unsupported,
                "D3D12 texture transfers are not implemented in this resource slice");
        }
    }
    return Status::success();
}

[[nodiscard]] Status transition_direct3d_buffer(
    ID3D12GraphicsCommandList& commandList, Direct3DBufferResource& resource,
    D3D12_RESOURCE_STATES target) {
    std::lock_guard lock{resource.mutex};
    if (resource.memory == MemoryDomain::upload) {
        return target == D3D12_RESOURCE_STATE_COPY_SOURCE
                   ? Status::success()
                   : Status::failure(StatusCode::invalid_argument,
                                     "D3D12 upload buffers are copy sources only");
    }
    if (resource.memory == MemoryDomain::readback) {
        return target == D3D12_RESOURCE_STATE_COPY_DEST
                   ? Status::success()
                   : Status::failure(StatusCode::invalid_argument,
                                     "D3D12 readback buffers are copy destinations only");
    }
    if (resource.state == target) {
        return Status::success();
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = resource.state;
    barrier.Transition.StateAfter = target;
    commandList.ResourceBarrier(1, &barrier);
    resource.state = target;
    return Status::success();
}

[[nodiscard]] Status record_direct3d_commands(
    Direct3DContext& context, ID3D12GraphicsCommandList& commandList,
    std::span<const detail::NativeCommand> commands,
    std::vector<ComPtr<ID3D12Resource>>& transients) {
    for (const auto& command : commands) {
        if (command.kind == detail::NativeCommandKind::barrier) {
            continue;
        }
        const auto& transfer = command.transfer;
        if (transfer.kind == detail::NativeTransferKind::copy_buffer) {
            const auto source =
                std::static_pointer_cast<Direct3DBufferResource>(
                    transfer.source);
            const auto destination =
                std::static_pointer_cast<Direct3DBufferResource>(
                    transfer.destination);
            if (auto status = transition_direct3d_buffer(
                    commandList, *source, D3D12_RESOURCE_STATE_COPY_SOURCE);
                !status.ok()) {
                return status;
            }
            if (auto status = transition_direct3d_buffer(
                    commandList, *destination,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                !status.ok()) {
                return status;
            }
            commandList.CopyBufferRegion(
                destination->resource.Get(), transfer.buffer.destinationOffset,
                source->resource.Get(), transfer.buffer.sourceOffset,
                transfer.buffer.size);
            continue;
        }

        const auto destination =
            std::static_pointer_cast<Direct3DBufferResource>(
                transfer.destination);
        auto upload = create_direct3d_buffer_resource(
            context, transfer.buffer.size, MemoryDomain::upload);
        if (!upload.ok()) {
            return upload.status();
        }
        auto staging = std::move(upload).value();
        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        auto result = staging->Map(0, &readRange, &mapped);
        if (FAILED(result)) {
            return direct3d_failure(StatusCode::backend_error,
                                    "D3D12 fill staging map failed", result);
        }
        std::memset(mapped,
                    std::to_integer<unsigned char>(transfer.fillValue),
                    transfer.buffer.size);
        const D3D12_RANGE writtenRange{0, transfer.buffer.size};
        staging->Unmap(0, &writtenRange);
        if (auto status = transition_direct3d_buffer(
                commandList, *destination, D3D12_RESOURCE_STATE_COPY_DEST);
            !status.ok()) {
            return status;
        }
        commandList.CopyBufferRegion(
            destination->resource.Get(), transfer.buffer.destinationOffset,
            staging.Get(), 0, transfer.buffer.size);
        transients.push_back(std::move(staging));
    }
    return Status::success();
}

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
    if (!waits.empty() || !signals.empty()) {
        return Status::failure(
            StatusCode::unsupported,
            "D3D12 timeline semaphore submission is not implemented");
    }
    const auto context =
        std::static_pointer_cast<Direct3DContext>(nativeContext);
    if (!context || !context->device || !context->queue || !context->fence) {
        return Status::failure(StatusCode::device_lost,
                               "the D3D12 native context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    if (auto status = validate_direct3d_commands(commands); !status.ok()) {
        return status;
    }
    ComPtr<ID3D12CommandAllocator> allocator;
    auto result = context->device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 command-allocator creation failed",
                                result);
    }
    ComPtr<ID3D12GraphicsCommandList> commandList;
    result = context->device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&commandList));
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 command-list creation failed", result);
    }
    std::vector<ComPtr<ID3D12Resource>> transients;
    if (auto status = record_direct3d_commands(
            *context, *commandList.Get(), commands, transients);
        !status.ok()) {
        return status;
    }
    result = commandList->Close();
    if (FAILED(result)) {
        return direct3d_failure(StatusCode::backend_error,
                                "D3D12 command-list close failed", result);
    }
    ID3D12CommandList* lists[] = {commandList.Get()};
    context->queue->ExecuteCommandLists(1, lists);
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
    config.supportedFeatures = {Feature::transfer, Feature::memory_budget};
    config.resourceCapabilities = {
        .bufferViews = true,
        .textureViews = false,
        .hostCoherent = true,
        .bufferCopy = true,
        .bufferFill = true,
        .bufferTextureCopy = false,
        .textureCopy = false,
        .textureClear = false,
        .textureResolve = false,
        .textureBlitNearest = false,
        .textureBlitLinear = false,
        .externalImport = false,
        .externalExport = false,
    };
    config.deviceLocalBudgetBytes = native.deviceLocalBudget;
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.createBuffer = &create_direct3d_buffer;
    config.mapBuffer = &map_direct3d_buffer;
    config.unmapBuffer = &unmap_direct3d_buffer;
    config.flushBuffer = &direct3d_buffer_range;
    config.invalidateBuffer = &direct3d_buffer_range;
    config.writeBuffer = &write_direct3d_buffer;
    config.readBuffer = &read_direct3d_buffer;
    config.nativeSubmit = &submit_direct3d_commands;
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::direct3d12, "Direct3D 12");
#endif
}

} // namespace truffle::rhi
