#include "truffle/rhi/rhi.hpp"

#include "foundation_backend.hpp"
#include "truffle/rhi/validation.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace truffle::rhi {

const ResourceBinding* PipelineReflection::find(std::uint32_t group,
                                                std::uint32_t binding,
                                                ShaderStage stage) const noexcept {
    for (const auto& candidate : bindings_) {
        if (candidate.group == group && candidate.binding == binding &&
            candidate.stage == stage) {
            return &candidate;
        }
    }
    return nullptr;
}

namespace detail {

using Handle = std::uint64_t;

enum class ObjectKind {
    instance,
    adapter,
    device,
    queue,
    command_pool,
    command_list,
    buffer,
    texture,
    shader,
    pipeline,
    compute_pipeline,
    fence,
    semaphore,
    query_pool,
    surface,
    swapchain,
    upload_ring,
};

struct Runtime;

struct BackendDispatch {
    Result<Handle> (*create_adapter)(Runtime&, std::size_t);
    Result<Handle> (*create_device)(Runtime&, Handle, const DeviceDesc&);
    Result<Handle> (*create_queue)(Runtime&, Handle, QueueKind);
    Result<Handle> (*create_command_pool)(Runtime&, Handle, QueueKind);
    Result<Handle> (*allocate_command_list)(Runtime&, Handle);
    Result<Handle> (*create_buffer)(Runtime&, Handle, const BufferDesc&);
    Result<Handle> (*create_texture)(Runtime&, Handle, const TextureDesc&);
    Result<Handle> (*create_shader)(Runtime&, Handle, const ShaderDesc&);
    Result<Handle> (*create_pipeline)(Runtime&, Handle, const PipelineDesc&);
    Result<Handle> (*create_compute_pipeline)(Runtime&, Handle,
                                               const ComputePipelineDesc&);
    Result<Handle> (*create_fence)(Runtime&, Handle, const FenceDesc&);
    Result<Handle> (*create_semaphore)(Runtime&, Handle, const SemaphoreDesc&);
    Result<Handle> (*create_query_pool)(Runtime&, Handle, const QueryPoolDesc&);
    Result<Handle> (*create_surface)(Runtime&, Handle, const SurfaceDesc&);
    Result<Handle> (*create_swapchain)(Runtime&, Handle, Handle,
                                       const SwapchainDesc&);
    Result<Handle> (*create_upload_ring)(Runtime&, Handle, std::uint32_t,
                                         std::size_t);
    Status (*submit)(Runtime&, Handle, std::span<const Handle>, Handle,
                     std::uint64_t);
    Status (*present)(Runtime&, Handle, Handle, std::uint32_t);
};

struct AdapterPayload {
    AdapterInfo info;
};

struct DevicePayload {
    AdapterInfo adapter;
    std::vector<Feature> enabledFeatures;
    std::atomic<bool> lost{false};
};

struct QueuePayload {
    QueueKind kind = QueueKind::graphics;
    std::mutex submitMutex;
};

struct CommandPoolPayload {
    QueueKind kind = QueueKind::graphics;
    std::thread::id owner;
};

struct CommandListPayload {
    QueueKind kind = QueueKind::graphics;
    CommandListState state = CommandListState::initial;
    std::thread::id owner;
    std::uint32_t activeEncoder = 0;
    bool graphicsPipelineBound = false;
    bool computePipelineBound = false;
    std::vector<std::shared_ptr<void>> retained;
    std::mutex mutex;
};

struct BufferPayload {
    explicit BufferPayload(BufferDesc value)
        : desc(std::move(value)), bytes(desc.size) {}
    BufferDesc desc;
    std::vector<std::byte> bytes;
    bool mapped = false;
    mutable std::mutex mutex;
};

struct TexturePayload {
    explicit TexturePayload(TextureDesc value) : desc(std::move(value)) {}
    TextureDesc desc;
};

struct ShaderPayload {
    explicit ShaderPayload(ShaderDesc value)
        : desc(std::move(value)), reflection(desc.reflection) {}
    ShaderDesc desc;
    PipelineReflection reflection;
};

struct PipelinePayload {
    PipelineReflection reflection;
};

struct ComputePipelinePayload {
    PipelineReflection reflection;
    Extent3D preferredWorkgroupSize{64, 1, 1};
};

struct FencePayload {
    explicit FencePayload(std::uint64_t initial) : value(initial) {}
    std::uint64_t value = 0;
    std::mutex mutex;
    std::condition_variable changed;
};

struct SemaphorePayload {
    explicit SemaphorePayload(std::uint64_t initial) : value(initial) {}
    std::atomic<std::uint64_t> value{0};
};

struct QueryPoolPayload {
    QueryPoolDesc desc;
};

struct SurfacePayload {
    SurfaceDesc desc;
};

struct SwapchainPayload {
    SwapchainDesc desc;
    std::shared_ptr<TexturePayload> image;
    std::uint32_t nextImage = 0;
    bool acquired = false;
    mutable std::mutex mutex;
};

struct UploadRingPayload {
    std::vector<std::shared_ptr<BufferPayload>> frames;
    std::uint32_t currentFrame = 0;
    std::size_t offset = 0;
    std::size_t bytesPerFrame = 0;
    std::mutex mutex;
};

struct Runtime : std::enable_shared_from_this<Runtime> {
    struct Slot {
        std::uint32_t generation = 1;
        ObjectKind kind = ObjectKind::instance;
        std::shared_ptr<void> payload;
        bool occupied = false;
    };

    InstanceDesc instanceDesc;
    FoundationBackendConfig config;
    const BackendDispatch* dispatch = nullptr;
    mutable std::mutex mutex;
    std::vector<Slot> slots;
    BackendStats stats;

    [[nodiscard]] Handle allocate(ObjectKind kind, std::shared_ptr<void> payload) {
        std::lock_guard lock{mutex};
        for (std::size_t index = 0; index < slots.size(); ++index) {
            auto& slot = slots[index];
            if (slot.occupied) {
                continue;
            }
            slot.kind = kind;
            slot.payload = std::move(payload);
            slot.occupied = true;
            return (static_cast<Handle>(slot.generation) << 32u) |
                   static_cast<Handle>(index + 1);
        }
        slots.push_back(Slot{1, kind, std::move(payload), true});
        return (static_cast<Handle>(1) << 32u) |
               static_cast<Handle>(slots.size());
    }

    [[nodiscard]] bool valid(ObjectKind kind, Handle handle) const noexcept {
        if (handle == 0) {
            return false;
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        return index < slots.size() && slots[index].occupied &&
               slots[index].generation == generation && slots[index].kind == kind;
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> resolve(ObjectKind kind, Handle handle) const {
        if (handle == 0) {
            return {};
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        if (index >= slots.size()) {
            return {};
        }
        const auto& slot = slots[index];
        if (!slot.occupied || slot.generation != generation || slot.kind != kind) {
            return {};
        }
        return std::static_pointer_cast<T>(slot.payload);
    }

    [[nodiscard]] std::shared_ptr<void> retain(ObjectKind kind, Handle handle) const {
        if (handle == 0) {
            return {};
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        if (index >= slots.size()) {
            return {};
        }
        const auto& slot = slots[index];
        if (!slot.occupied || slot.generation != generation || slot.kind != kind) {
            return {};
        }
        return slot.payload;
    }

    void release(ObjectKind kind, Handle handle) noexcept {
        if (handle == 0) {
            return;
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        if (index >= slots.size()) {
            return;
        }
        auto& slot = slots[index];
        if (!slot.occupied || slot.generation != generation || slot.kind != kind) {
            return;
        }
        slot.payload.reset();
        slot.occupied = false;
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
    }

    template <typename Function>
    void update_stats(Function&& function) {
        std::lock_guard lock{mutex};
        function(stats);
    }

    [[nodiscard]] BackendStats stats_snapshot() const noexcept {
        std::lock_guard lock{mutex};
        return stats;
    }
};

struct ObjectState {
    ObjectState(std::shared_ptr<Runtime> runtimeValue, ObjectKind kindValue,
                Handle handleValue)
        : runtime(std::move(runtimeValue)), kind(kindValue), handle(handleValue) {}

    ~ObjectState() {
        if (runtime) {
            runtime->release(kind, handle);
        }
    }

    std::shared_ptr<Runtime> runtime;
    ObjectKind kind = ObjectKind::instance;
    Handle handle = 0;
};

struct SwapchainState {
    explicit SwapchainState(std::unique_ptr<ObjectState> objectValue)
        : object(std::move(objectValue)) {}
    std::unique_ptr<ObjectState> object;
    std::unique_ptr<Texture> image;
    std::unique_ptr<Semaphore> available;
};

struct UploadRingState {
    explicit UploadRingState(std::unique_ptr<ObjectState> objectValue)
        : object(std::move(objectValue)) {}
    std::unique_ptr<ObjectState> object;
    std::vector<Buffer> buffers;
};

[[nodiscard]] Status invalid_object(std::string object) {
    return Status::failure(StatusCode::invalid_state,
                           std::move(object) + " is invalid or stale");
}

[[nodiscard]] Status unsupported(Runtime& runtime, std::string operation) {
    return Status::failure(
        StatusCode::unsupported,
        std::move(operation) + " is not implemented by the " +
            runtime.config.adapterName + " RHI 1 foundation");
}

[[nodiscard]] std::unique_ptr<ObjectState> make_state(
    const std::shared_ptr<Runtime>& runtime, ObjectKind kind, Handle handle) {
    return std::make_unique<ObjectState>(runtime, kind, handle);
}

struct Factory {
    [[nodiscard]] static Instance instance(const std::shared_ptr<Runtime>& runtime,
                                           Handle handle) {
        return Instance{make_state(runtime, ObjectKind::instance, handle)};
    }
    [[nodiscard]] static Adapter adapter(const std::shared_ptr<Runtime>& runtime,
                                         Handle handle) {
        return Adapter{make_state(runtime, ObjectKind::adapter, handle)};
    }
    [[nodiscard]] static Device device(const std::shared_ptr<Runtime>& runtime,
                                       Handle handle) {
        return Device{make_state(runtime, ObjectKind::device, handle)};
    }
    [[nodiscard]] static Queue queue(const std::shared_ptr<Runtime>& runtime,
                                     Handle handle) {
        return Queue{make_state(runtime, ObjectKind::queue, handle)};
    }
    [[nodiscard]] static CommandPool command_pool(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return CommandPool{make_state(runtime, ObjectKind::command_pool, handle)};
    }
    [[nodiscard]] static CommandList command_list(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return CommandList{make_state(runtime, ObjectKind::command_list, handle)};
    }
    [[nodiscard]] static Buffer buffer(const std::shared_ptr<Runtime>& runtime,
                                       Handle handle) {
        return Buffer{make_state(runtime, ObjectKind::buffer, handle)};
    }
    [[nodiscard]] static Texture texture(const std::shared_ptr<Runtime>& runtime,
                                         Handle handle) {
        return Texture{make_state(runtime, ObjectKind::texture, handle)};
    }
    [[nodiscard]] static Shader shader(const std::shared_ptr<Runtime>& runtime,
                                       Handle handle) {
        return Shader{make_state(runtime, ObjectKind::shader, handle)};
    }
    [[nodiscard]] static Pipeline pipeline(const std::shared_ptr<Runtime>& runtime,
                                           Handle handle) {
        return Pipeline{make_state(runtime, ObjectKind::pipeline, handle)};
    }
    [[nodiscard]] static ComputePipeline compute_pipeline(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return ComputePipeline{
            make_state(runtime, ObjectKind::compute_pipeline, handle)};
    }
    [[nodiscard]] static Fence fence(const std::shared_ptr<Runtime>& runtime,
                                     Handle handle) {
        return Fence{make_state(runtime, ObjectKind::fence, handle)};
    }
    [[nodiscard]] static Semaphore semaphore(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return Semaphore{make_state(runtime, ObjectKind::semaphore, handle)};
    }
    [[nodiscard]] static QueryPool query_pool(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return QueryPool{make_state(runtime, ObjectKind::query_pool, handle)};
    }
    [[nodiscard]] static Surface surface(const std::shared_ptr<Runtime>& runtime,
                                         Handle handle) {
        return Surface{make_state(runtime, ObjectKind::surface, handle)};
    }
    [[nodiscard]] static Swapchain swapchain(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return Swapchain{std::make_unique<SwapchainState>(
            make_state(runtime, ObjectKind::swapchain, handle))};
    }
    [[nodiscard]] static UploadRing upload_ring(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        auto state = std::make_unique<UploadRingState>(
            make_state(runtime, ObjectKind::upload_ring, handle));
        const auto payload = runtime->resolve<UploadRingPayload>(
            ObjectKind::upload_ring, handle);
        if (payload) {
            state->buffers.reserve(payload->frames.size());
            for (const auto& frame : payload->frames) {
                const auto bufferHandle = runtime->allocate(ObjectKind::buffer, frame);
                state->buffers.push_back(buffer(runtime, bufferHandle));
            }
        }
        return UploadRing{std::move(state)};
    }
};

[[nodiscard]] Result<Handle> foundation_create_adapter(Runtime& runtime,
                                                       std::size_t index) {
    if (index != 0 || runtime.config.adapterName.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "adapter index is out of range");
    }
    AdapterInfo info;
    info.name = runtime.config.adapterName;
    info.backend = runtime.config.kind;
    info.native = runtime.config.native;
    info.validationOnly = runtime.config.validationOnly;
    info.presentation = runtime.config.presentation;
    info.queueKinds = runtime.config.queueKinds;
    info.supportedFeatures = runtime.config.supportedFeatures;
    return runtime.allocate(ObjectKind::adapter,
                            std::make_shared<AdapterPayload>(
                                AdapterPayload{std::move(info)}));
}

[[nodiscard]] Result<Handle> foundation_create_device(Runtime& runtime,
                                                      Handle adapterHandle,
                                                      const DeviceDesc& desc) {
    const auto adapter = runtime.resolve<AdapterPayload>(ObjectKind::adapter,
                                                         adapterHandle);
    if (!adapter) {
        return invalid_object("adapter");
    }
    std::vector<Feature> enabled = desc.requiredFeatures;
    for (const auto feature : desc.requiredFeatures) {
        if (!validation::supports_feature(adapter->info, feature)) {
            return Status::failure(StatusCode::unsupported,
                                   "required device feature is unsupported");
        }
    }
    for (const auto feature : desc.optionalFeatures) {
        if (validation::supports_feature(adapter->info, feature) &&
            std::find(enabled.begin(), enabled.end(), feature) == enabled.end()) {
            enabled.push_back(feature);
        }
    }
    auto payload = std::make_shared<DevicePayload>();
    payload->adapter = adapter->info;
    payload->enabledFeatures = std::move(enabled);
    runtime.update_stats([](BackendStats& stats) { ++stats.devicesCreated; });
    return runtime.allocate(ObjectKind::device, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_queue(Runtime& runtime,
                                                     Handle deviceHandle,
                                                     QueueKind kind) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::supports_queue(device->adapter, kind)) {
        return Status::failure(StatusCode::unsupported,
                               "requested queue kind is unsupported");
    }
    auto payload = std::make_shared<QueuePayload>();
    payload->kind = kind;
    return runtime.allocate(ObjectKind::queue, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_command_pool(Runtime& runtime,
                                                            Handle deviceHandle,
                                                            QueueKind kind) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::supports_queue(device->adapter, kind)) {
        return Status::failure(StatusCode::unsupported,
                               "requested queue kind is unsupported");
    }
    auto payload = std::make_shared<CommandPoolPayload>();
    payload->kind = kind;
    payload->owner = std::this_thread::get_id();
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.commandPoolsCreated; });
    return runtime.allocate(ObjectKind::command_pool, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_allocate_command_list(Runtime& runtime,
                                                              Handle poolHandle) {
    const auto pool = runtime.resolve<CommandPoolPayload>(ObjectKind::command_pool,
                                                          poolHandle);
    if (!pool) {
        return invalid_object("command pool");
    }
    if (pool->owner != std::this_thread::get_id()) {
        return Status::failure(StatusCode::invalid_state,
                               "command pool is owned by another thread");
    }
    auto payload = std::make_shared<CommandListPayload>();
    payload->kind = pool->kind;
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.commandListsCreated; });
    return runtime.allocate(ObjectKind::command_list, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_buffer(Runtime& runtime,
                                                      Handle deviceHandle,
                                                      const BufferDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "buffer creation");
    }
    if (!validation::buffer_desc_valid(desc)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer size and usage must be non-zero");
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.buffersCreated; });
    return runtime.allocate(ObjectKind::buffer,
                            std::make_shared<BufferPayload>(desc));
}

[[nodiscard]] Result<Handle> foundation_create_texture(Runtime& runtime,
                                                       Handle deviceHandle,
                                                       const TextureDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "texture creation");
    }
    if (!validation::texture_desc_valid(desc)) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture descriptor is invalid");
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.texturesCreated; });
    return runtime.allocate(ObjectKind::texture,
                            std::make_shared<TexturePayload>(desc));
}

[[nodiscard]] Result<Handle> foundation_create_shader(Runtime& runtime,
                                                      Handle deviceHandle,
                                                      const ShaderDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "shader creation");
    }
    if (desc.entryPoint.empty() || desc.code.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "shader entry point and code are required");
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.shadersCreated; });
    return runtime.allocate(ObjectKind::shader,
                            std::make_shared<ShaderPayload>(desc));
}

[[nodiscard]] std::vector<ResourceBinding> merge_reflection(
    const std::shared_ptr<ShaderPayload>& first,
    const std::shared_ptr<ShaderPayload>& second = {}) {
    std::vector<ResourceBinding> bindings;
    if (first) {
        const auto reflected = first->reflection.bindings();
        bindings.insert(bindings.end(), reflected.begin(), reflected.end());
    }
    if (second) {
        const auto reflected = second->reflection.bindings();
        bindings.insert(bindings.end(), reflected.begin(), reflected.end());
    }
    return bindings;
}

[[nodiscard]] Result<Handle> foundation_create_pipeline(Runtime& runtime,
                                                        Handle deviceHandle,
                                                        const PipelineDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "graphics pipeline creation");
    }
    std::shared_ptr<ShaderPayload> vertex;
    std::shared_ptr<ShaderPayload> fragment;
    if (desc.vertexShader != nullptr) {
        if (!desc.vertexShader->valid()) {
            return invalid_object("vertex shader");
        }
        vertex = runtime.resolve<ShaderPayload>(ObjectKind::shader,
                                                desc.vertexShader->id().value);
        if (!vertex || vertex->desc.stage != ShaderStage::vertex) {
            return Status::failure(StatusCode::invalid_argument,
                                   "vertex pipeline input must be a vertex shader");
        }
    }
    if (desc.fragmentShader != nullptr) {
        if (!desc.fragmentShader->valid()) {
            return invalid_object("fragment shader");
        }
        fragment = runtime.resolve<ShaderPayload>(ObjectKind::shader,
                                                  desc.fragmentShader->id().value);
        if (!fragment || fragment->desc.stage != ShaderStage::fragment) {
            return Status::failure(
                StatusCode::invalid_argument,
                "fragment pipeline input must be a fragment shader");
        }
    }
    auto payload = std::make_shared<PipelinePayload>();
    payload->reflection = PipelineReflection{merge_reflection(vertex, fragment)};
    runtime.update_stats([](BackendStats& stats) { ++stats.pipelinesCreated; });
    return runtime.allocate(ObjectKind::pipeline, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_compute_pipeline(
    Runtime& runtime, Handle deviceHandle, const ComputePipelineDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "compute pipeline creation");
    }
    if (desc.computeShader == nullptr || !desc.computeShader->valid()) {
        return Status::failure(StatusCode::invalid_argument,
                               "compute pipeline requires a valid shader");
    }
    const auto shader = runtime.resolve<ShaderPayload>(
        ObjectKind::shader, desc.computeShader->id().value);
    if (!shader || shader->desc.stage != ShaderStage::compute) {
        return Status::failure(StatusCode::invalid_argument,
                               "compute pipeline requires a compute shader");
    }
    auto payload = std::make_shared<ComputePipelinePayload>();
    payload->reflection = PipelineReflection{merge_reflection(shader)};
    payload->preferredWorkgroupSize = desc.preferredWorkgroupSize;
    runtime.update_stats([](BackendStats& stats) { ++stats.pipelinesCreated; });
    return runtime.allocate(ObjectKind::compute_pipeline, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_fence(Runtime& runtime,
                                                     Handle deviceHandle,
                                                     const FenceDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "fence creation");
    }
    return runtime.allocate(ObjectKind::fence,
                            std::make_shared<FencePayload>(desc.initialValue));
}

[[nodiscard]] Result<Handle> foundation_create_semaphore(
    Runtime& runtime, Handle deviceHandle, const SemaphoreDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "semaphore creation");
    }
    return runtime.allocate(
        ObjectKind::semaphore,
        std::make_shared<SemaphorePayload>(desc.initialValue));
}

[[nodiscard]] Result<Handle> foundation_create_query_pool(
    Runtime& runtime, Handle deviceHandle, const QueryPoolDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "query pool creation");
    }
    if (desc.count == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "query pool count must be non-zero");
    }
    return runtime.allocate(ObjectKind::query_pool,
                            std::make_shared<QueryPoolPayload>(
                                QueryPoolPayload{desc}));
}

[[nodiscard]] Result<Handle> foundation_create_surface(Runtime& runtime,
                                                       Handle deviceHandle,
                                                       const SurfaceDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!validation::is_non_zero(desc.initialExtent) ||
        !validation::native_surface_handles_valid(desc.native)) {
        return Status::failure(StatusCode::invalid_argument,
                               "surface descriptor is invalid");
    }
    if (!runtime.config.presentation) {
        return unsupported(runtime, "surface creation");
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.surfacesCreated; });
    return runtime.allocate(ObjectKind::surface,
                            std::make_shared<SurfacePayload>(
                                SurfacePayload{desc}));
}

[[nodiscard]] Result<Handle> foundation_create_swapchain(
    Runtime& runtime, Handle deviceHandle, Handle surfaceHandle,
    const SwapchainDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.resolve<SurfacePayload>(ObjectKind::surface, surfaceHandle)) {
        return invalid_object("surface");
    }
    if (!validation::is_non_zero(desc.extent) || desc.imageCount == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "swapchain extent and image count must be non-zero");
    }
    auto payload = std::make_shared<SwapchainPayload>();
    payload->desc = desc;
    payload->image = std::make_shared<TexturePayload>(TextureDesc{
        .extent = {desc.extent.width, desc.extent.height, 1},
        .format = desc.format,
        .usage = TextureUsage::color_attachment | TextureUsage::present,
        .debugName = desc.debugName + " image",
    });
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.swapchainsCreated; });
    return runtime.allocate(ObjectKind::swapchain, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_upload_ring(
    Runtime& runtime, Handle deviceHandle, std::uint32_t frameCount,
    std::size_t bytesPerFrame) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "upload ring creation");
    }
    if (frameCount == 0 || bytesPerFrame == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "upload ring dimensions must be non-zero");
    }
    auto payload = std::make_shared<UploadRingPayload>();
    payload->bytesPerFrame = bytesPerFrame;
    payload->frames.reserve(frameCount);
    for (std::uint32_t index = 0; index < frameCount; ++index) {
        payload->frames.push_back(std::make_shared<BufferPayload>(BufferDesc{
            .size = bytesPerFrame,
            .usage = BufferUsage::vertex | BufferUsage::uniform |
                     BufferUsage::storage | BufferUsage::copy_source,
            .memory = MemoryDomain::upload,
            .mappedAtCreation = true,
            .debugName = "frame upload " + std::to_string(index),
        }));
    }
    return runtime.allocate(ObjectKind::upload_ring, std::move(payload));
}

[[nodiscard]] Status foundation_submit(Runtime& runtime, Handle queueHandle,
                                       std::span<const Handle> commandLists,
                                       Handle fenceHandle,
                                       std::uint64_t fenceValue) {
    const auto queue = runtime.resolve<QueuePayload>(ObjectKind::queue, queueHandle);
    if (!queue) {
        return invalid_object("queue");
    }
    std::lock_guard queueLock{queue->submitMutex};
    std::vector<std::shared_ptr<CommandListPayload>> lists;
    lists.reserve(commandLists.size());
    for (const auto handle : commandLists) {
        auto list = runtime.resolve<CommandListPayload>(ObjectKind::command_list,
                                                        handle);
        if (!list) {
            return invalid_object("command list");
        }
        std::lock_guard listLock{list->mutex};
        if (list->state != CommandListState::executable) {
            return Status::failure(StatusCode::invalid_state,
                                   "submitted command list is not executable");
        }
        if (list->kind != queue->kind) {
            return Status::failure(StatusCode::invalid_argument,
                                   "command list queue kind does not match queue");
        }
        lists.push_back(std::move(list));
    }
    if (runtime.config.nativeSubmit != nullptr) {
        if (auto status = runtime.config.nativeSubmit(); !status.ok()) {
            return status;
        }
    }
    for (const auto& list : lists) {
        std::lock_guard listLock{list->mutex};
        list->state = CommandListState::submitted;
        list->retained.clear();
    }
    if (fenceHandle != 0) {
        const auto fence = runtime.resolve<FencePayload>(ObjectKind::fence,
                                                         fenceHandle);
        if (!fence) {
            return invalid_object("fence");
        }
        {
            std::lock_guard fenceLock{fence->mutex};
            fence->value = std::max(fence->value, fenceValue);
        }
        fence->changed.notify_all();
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.submissions; });
    return Status::success();
}

[[nodiscard]] Status foundation_present(Runtime& runtime, Handle queueHandle,
                                        Handle swapchainHandle,
                                        std::uint32_t imageIndex) {
    const auto queue = runtime.resolve<QueuePayload>(ObjectKind::queue, queueHandle);
    const auto swapchain = runtime.resolve<SwapchainPayload>(
        ObjectKind::swapchain, swapchainHandle);
    if (!queue) {
        return invalid_object("queue");
    }
    if (!swapchain) {
        return invalid_object("swapchain");
    }
    if (queue->kind != QueueKind::graphics) {
        return Status::failure(StatusCode::invalid_argument,
                               "presentation requires a graphics queue");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "presentation");
    }
    std::lock_guard lock{swapchain->mutex};
    if (!swapchain->acquired || imageIndex >= swapchain->desc.imageCount) {
        return Status::failure(StatusCode::invalid_state,
                               "present requires an acquired swapchain image");
    }
    swapchain->acquired = false;
    runtime.update_stats([](BackendStats& stats) { ++stats.presentations; });
    return Status::success();
}

const BackendDispatch kFoundationDispatch{
    &foundation_create_adapter,
    &foundation_create_device,
    &foundation_create_queue,
    &foundation_create_command_pool,
    &foundation_allocate_command_list,
    &foundation_create_buffer,
    &foundation_create_texture,
    &foundation_create_shader,
    &foundation_create_pipeline,
    &foundation_create_compute_pipeline,
    &foundation_create_fence,
    &foundation_create_semaphore,
    &foundation_create_query_pool,
    &foundation_create_surface,
    &foundation_create_swapchain,
    &foundation_create_upload_ring,
    &foundation_submit,
    &foundation_present,
};

Result<Instance> create_foundation_instance(const InstanceDesc& desc,
                                            FoundationBackendConfig config) {
    if (config.adapterName.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "foundation backend requires an adapter name");
    }
    auto runtime = std::make_shared<Runtime>();
    runtime->instanceDesc = desc;
    runtime->config = std::move(config);
    runtime->dispatch = &kFoundationDispatch;
    const auto handle = runtime->allocate(ObjectKind::instance,
                                          std::make_shared<int>(0));
    return Factory::instance(runtime, handle);
}

Result<Instance> unavailable_backend(BackendKind kind, std::string backendName) {
    (void)kind;
    return Status::failure(
        StatusCode::unsupported,
        std::move(backendName) +
            " has no native RHI 1 implementation; no simulated adapter is exposed");
}

[[nodiscard]] bool state_valid(const std::unique_ptr<ObjectState>& state) noexcept {
    return state && state->runtime &&
           state->runtime->valid(state->kind, state->handle);
}

[[nodiscard]] ObjectId state_id(const std::unique_ptr<ObjectState>& state) noexcept {
    return state_valid(state) ? ObjectId{state->handle} : ObjectId{};
}

template <typename Payload>
[[nodiscard]] std::shared_ptr<Payload> payload(
    const std::unique_ptr<ObjectState>& state, ObjectKind kind) {
    if (!state || !state->runtime) {
        return {};
    }
    return state->runtime->resolve<Payload>(kind, state->handle);
}

} // namespace detail

#define TRUFFLE_DEFINE_OBJECT_LIFETIME(Type)                                      \
    Type::Type() noexcept = default;                                               \
    Type::~Type() = default;                                                       \
    Type::Type(Type&&) noexcept = default;                                         \
    Type& Type::operator=(Type&&) noexcept = default;                              \
    Type::Type(std::unique_ptr<detail::ObjectState> state) noexcept                \
        : state_(std::move(state)) {}                                              \
    bool Type::valid() const noexcept { return detail::state_valid(state_); }       \
    ObjectId Type::id() const noexcept { return detail::state_id(state_); }

TRUFFLE_DEFINE_OBJECT_LIFETIME(Instance)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Adapter)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Device)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Queue)
TRUFFLE_DEFINE_OBJECT_LIFETIME(CommandPool)
TRUFFLE_DEFINE_OBJECT_LIFETIME(CommandList)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Buffer)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Texture)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Shader)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Pipeline)
TRUFFLE_DEFINE_OBJECT_LIFETIME(ComputePipeline)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Fence)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Semaphore)
TRUFFLE_DEFINE_OBJECT_LIFETIME(QueryPool)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Surface)

#undef TRUFFLE_DEFINE_OBJECT_LIFETIME

BackendKind Instance::backend() const noexcept {
    return valid() ? state_->runtime->config.kind : BackendKind::null_validation;
}

std::size_t Instance::adapter_count() const noexcept {
    return valid() && !state_->runtime->config.adapterName.empty() ? 1u : 0u;
}

Result<Adapter> Instance::adapter(std::size_t index) const {
    if (!valid()) {
        return detail::invalid_object("instance");
    }
    auto handle = state_->runtime->dispatch->create_adapter(*state_->runtime, index);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::adapter(state_->runtime, handle.value());
}

BackendStats Instance::stats() const noexcept {
    return valid() ? state_->runtime->stats_snapshot() : BackendStats{};
}

const AdapterInfo& Adapter::info() const {
    const auto value = detail::payload<detail::AdapterPayload>(
        state_, detail::ObjectKind::adapter);
    if (!value) {
        static const AdapterInfo invalid{};
        return invalid;
    }
    return value->info;
}

Result<Device> Adapter::request_device(const DeviceDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("adapter");
    }
    auto handle = state_->runtime->dispatch->create_device(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::device(state_->runtime, handle.value());
}

bool Device::lost() const noexcept {
    const auto value = detail::payload<detail::DevicePayload>(
        state_, detail::ObjectKind::device);
    return !value || value->lost.load();
}

const AdapterInfo& Device::adapter_info() const {
    const auto value = detail::payload<detail::DevicePayload>(
        state_, detail::ObjectKind::device);
    if (!value) {
        static const AdapterInfo invalid{};
        return invalid;
    }
    return value->adapter;
}

Result<Queue> Device::queue(QueueKind kind) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_queue(
        *state_->runtime, state_->handle, kind);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::queue(state_->runtime, handle.value());
}

Result<CommandPool> Device::create_command_pool(QueueKind kind) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_command_pool(
        *state_->runtime, state_->handle, kind);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::command_pool(state_->runtime, handle.value());
}

Result<Buffer> Device::create_buffer(const BufferDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_buffer(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::buffer(state_->runtime, handle.value());
}

Result<Texture> Device::create_texture(const TextureDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_texture(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::texture(state_->runtime, handle.value());
}

Result<Shader> Device::create_shader(const ShaderDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_shader(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::shader(state_->runtime, handle.value());
}

Result<Pipeline> Device::create_pipeline(const PipelineDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    const auto shader_belongs_to_device = [this](const Shader* shader) {
        return shader == nullptr ||
               (shader->valid() &&
                shader->state_->runtime.get() == state_->runtime.get());
    };
    if (!shader_belongs_to_device(desc.vertexShader) ||
        !shader_belongs_to_device(desc.fragmentShader)) {
        return Status::failure(StatusCode::invalid_argument,
                               "pipeline shaders must belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_pipeline(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::pipeline(state_->runtime, handle.value());
}

Result<ComputePipeline> Device::create_compute_pipeline(
    const ComputePipelineDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    if (desc.computeShader != nullptr &&
        (!desc.computeShader->valid() ||
         desc.computeShader->state_->runtime.get() != state_->runtime.get())) {
        return Status::failure(
            StatusCode::invalid_argument,
            "compute pipeline shader must belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_compute_pipeline(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::compute_pipeline(state_->runtime, handle.value());
}

Result<Fence> Device::create_fence(const FenceDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_fence(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::fence(state_->runtime, handle.value());
}

Result<Semaphore> Device::create_semaphore(const SemaphoreDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_semaphore(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::semaphore(state_->runtime, handle.value());
}

Result<QueryPool> Device::create_query_pool(const QueryPoolDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_query_pool(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::query_pool(state_->runtime, handle.value());
}

Result<Surface> Device::create_surface(const SurfaceDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_surface(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::surface(state_->runtime, handle.value());
}

Result<Swapchain> Device::create_swapchain(Surface& surface,
                                           const SwapchainDesc& desc) const {
    if (!valid() || !surface.valid() ||
        surface.state_->runtime.get() != state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "surface must be valid and belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_swapchain(
        *state_->runtime, state_->handle, surface.state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::swapchain(state_->runtime, handle.value());
}

Result<UploadRing> Device::create_upload_ring(std::uint32_t frameCount,
                                              std::size_t bytesPerFrame) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_upload_ring(
        *state_->runtime, state_->handle, frameCount, bytesPerFrame);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::upload_ring(state_->runtime, handle.value());
}

BufferDesc Buffer::desc() const {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    return value ? value->desc : BufferDesc{};
}

Result<std::span<std::byte>> Buffer::map() {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    std::lock_guard lock{value->mutex};
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local buffers are not host mappable");
    }
    value->mapped = true;
    return std::span<std::byte>{value->bytes};
}

Status Buffer::unmap() {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    std::lock_guard lock{value->mutex};
    value->mapped = false;
    return Status::success();
}

Status Buffer::write(std::size_t offset, std::span<const std::byte> data) {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    std::lock_guard lock{value->mutex};
    if (offset > value->bytes.size() || data.size() > value->bytes.size() - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer write exceeds allocation");
    }
    std::copy(data.begin(), data.end(), value->bytes.begin() +
                                             static_cast<std::ptrdiff_t>(offset));
    return Status::success();
}

Status Buffer::read(std::size_t offset, std::span<std::byte> data) const {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    std::lock_guard lock{value->mutex};
    if (offset > value->bytes.size() || data.size() > value->bytes.size() - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer read exceeds allocation");
    }
    std::copy(value->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
              value->bytes.begin() + static_cast<std::ptrdiff_t>(offset + data.size()),
              data.begin());
    return Status::success();
}

TextureDesc Texture::desc() const {
    const auto value = detail::payload<detail::TexturePayload>(
        state_, detail::ObjectKind::texture);
    return value ? value->desc : TextureDesc{};
}

ShaderStage Shader::stage() const {
    const auto value = detail::payload<detail::ShaderPayload>(
        state_, detail::ObjectKind::shader);
    return value ? value->desc.stage : ShaderStage::vertex;
}

const PipelineReflection& Shader::reflection() const {
    const auto value = detail::payload<detail::ShaderPayload>(
        state_, detail::ObjectKind::shader);
    if (!value) {
        static const PipelineReflection empty;
        return empty;
    }
    return value->reflection;
}

const PipelineReflection& Pipeline::reflection() const {
    const auto value = detail::payload<detail::PipelinePayload>(
        state_, detail::ObjectKind::pipeline);
    if (!value) {
        static const PipelineReflection empty;
        return empty;
    }
    return value->reflection;
}

const PipelineReflection& ComputePipeline::reflection() const {
    const auto value = detail::payload<detail::ComputePipelinePayload>(
        state_, detail::ObjectKind::compute_pipeline);
    if (!value) {
        static const PipelineReflection empty;
        return empty;
    }
    return value->reflection;
}

Extent3D ComputePipeline::preferred_workgroup_size() const {
    const auto value = detail::payload<detail::ComputePipelinePayload>(
        state_, detail::ObjectKind::compute_pipeline);
    return value ? value->preferredWorkgroupSize : Extent3D{};
}

Result<CommandList> CommandPool::allocate() const {
    if (!valid()) {
        return detail::invalid_object("command pool");
    }
    auto handle = state_->runtime->dispatch->allocate_command_list(
        *state_->runtime, state_->handle);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::command_list(state_->runtime, handle.value());
}

Status CommandPool::reset() {
    const auto value = detail::payload<detail::CommandPoolPayload>(
        state_, detail::ObjectKind::command_pool);
    if (!value) {
        return detail::invalid_object("command pool");
    }
    if (value->owner != std::this_thread::get_id()) {
        return Status::failure(StatusCode::invalid_state,
                               "command pool is owned by another thread");
    }
    return Status::success();
}

CommandListState CommandList::state() const noexcept {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return CommandListState::invalid;
    }
    std::lock_guard lock{value->mutex};
    return value->state;
}

Status CommandList::begin() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->state != CommandListState::initial) {
        return Status::failure(StatusCode::invalid_state,
                               "command list begin requires initial state");
    }
    value->owner = std::this_thread::get_id();
    value->state = CommandListState::recording;
    value->graphicsPipelineBound = false;
    value->computePipelineBound = false;
    return Status::success();
}

Status CommandList::end() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id()) {
        return Status::failure(StatusCode::invalid_state,
                               "command list is owned by another thread");
    }
    if (value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "command list end requires recording with no active encoder");
    }
    value->state = CommandListState::executable;
    return Status::success();
}

Status CommandList::reset() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->state == CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "recording command list cannot be reset");
    }
    value->state = CommandListState::initial;
    value->owner = {};
    value->retained.clear();
    value->graphicsPipelineBound = false;
    value->computePipelineBound = false;
    return Status::success();
}

Result<RenderEncoder> CommandList::begin_rendering(const RenderPassDesc& desc) {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "render encoder requires owned recording command list");
    }
    if (!validation::is_non_zero(desc.extent)) {
        return Status::failure(StatusCode::invalid_argument,
                               "render extent must be non-zero");
    }
    std::vector<std::shared_ptr<void>> attachments;
    attachments.reserve(desc.colorAttachments.size());
    for (const auto& attachment : desc.colorAttachments) {
        if (attachment.texture == nullptr) {
            continue;
        }
        if (!attachment.texture->valid() ||
            attachment.texture->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "render attachments must belong to the command-list runtime");
        }
        auto retained = state_->runtime->retain(
            detail::ObjectKind::texture, attachment.texture->state_->handle);
        if (!retained) {
            return detail::invalid_object("render attachment");
        }
        attachments.push_back(std::move(retained));
    }
    value->retained.insert(value->retained.end(), attachments.begin(),
                           attachments.end());
    value->activeEncoder = 1;
    value->graphicsPipelineBound = false;
    return RenderEncoder{*this};
}

Result<ComputeEncoder> CommandList::begin_compute() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "compute encoder requires owned recording command list");
    }
    value->activeEncoder = 2;
    value->computePipelineBound = false;
    return ComputeEncoder{*this};
}

Result<CopyEncoder> CommandList::begin_copy() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "copy encoder requires owned recording command list");
    }
    value->activeEncoder = 3;
    return CopyEncoder{*this};
}

Status CommandList::encoder_command(std::uint32_t opcode, ObjectId object,
                                    std::uint64_t arg0, std::uint64_t arg1) {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder == 0) {
        return Status::failure(StatusCode::invalid_state,
                               "encoder command requires active owned recording");
    }
    switch (opcode) {
    case 1: {
        auto retained = state_->runtime->retain(detail::ObjectKind::pipeline,
                                                object.value);
        if (!retained || value->activeEncoder != 1) {
            return Status::failure(StatusCode::invalid_argument,
                                   "graphics pipeline is invalid for encoder");
        }
        value->retained.push_back(std::move(retained));
        value->graphicsPipelineBound = true;
        break;
    }
    case 2:
    case 3:
    case 5:
    case 7:
    case 8: {
        auto retained = state_->runtime->retain(detail::ObjectKind::buffer,
                                                object.value);
        if (!retained) {
            return detail::invalid_object("buffer");
        }
        value->retained.push_back(std::move(retained));
        break;
    }
    case 4: {
        auto retained = state_->runtime->retain(
            detail::ObjectKind::compute_pipeline, object.value);
        if (!retained || value->activeEncoder != 2) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline is invalid for encoder");
        }
        value->retained.push_back(std::move(retained));
        value->computePipelineBound = true;
        break;
    }
    case 6:
        if (value->activeEncoder != 1 || !value->graphicsPipelineBound ||
            arg0 == 0 || arg1 == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw requires a pipeline and non-zero counts");
        }
        state_->runtime->update_stats(
            [](BackendStats& stats) { ++stats.drawsRecorded; });
        break;
    case 9:
        if (value->activeEncoder != 2 || !value->computePipelineBound ||
            arg0 == 0 || arg1 == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch requires a pipeline and non-zero groups");
        }
        state_->runtime->update_stats(
            [](BackendStats& stats) { ++stats.dispatchesRecorded; });
        break;
    default:
        return Status::failure(StatusCode::invalid_argument,
                               "unknown encoder command");
    }
    return Status::success();
}

Status CommandList::end_encoder(std::uint32_t encoderKind) {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->activeEncoder != encoderKind) {
        return Status::failure(StatusCode::invalid_state,
                               "encoder end does not match active encoder");
    }
    value->activeEncoder = 0;
    return Status::success();
}

void CommandList::abandon_encoder(std::uint32_t encoderKind) noexcept {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return;
    }
    std::lock_guard lock{value->mutex};
    if (value->activeEncoder == encoderKind) {
        value->activeEncoder = 0;
        value->state = CommandListState::invalid;
    }
}

RenderEncoder::RenderEncoder() noexcept = default;
RenderEncoder::RenderEncoder(CommandList& list) noexcept
    : list_(&list), active_(true) {}
RenderEncoder::~RenderEncoder() {
    if (active_ && list_ != nullptr) {
        list_->abandon_encoder(1);
    }
}
RenderEncoder::RenderEncoder(RenderEncoder&& other) noexcept
    : list_(std::exchange(other.list_, nullptr)),
      active_(std::exchange(other.active_, false)) {}
RenderEncoder& RenderEncoder::operator=(RenderEncoder&& other) noexcept {
    if (this != &other) {
        if (active_ && list_ != nullptr) {
            list_->abandon_encoder(1);
        }
        list_ = std::exchange(other.list_, nullptr);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Status RenderEncoder::bind_pipeline(Pipeline& pipeline) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!pipeline.valid() ||
        pipeline.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "graphics pipeline belongs to another runtime");
    }
    return list_->encoder_command(1, pipeline.id());
}

Status RenderEncoder::bind_vertex_buffer(std::uint32_t slot, Buffer& buffer,
                                         std::size_t offset) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "vertex buffer belongs to another runtime");
    }
    return list_->encoder_command(2, buffer.id(), slot, offset);
}

Status RenderEncoder::bind_uniform_buffer(std::uint32_t slot, Buffer& buffer,
                                          std::size_t offset) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "uniform buffer belongs to another runtime");
    }
    return list_->encoder_command(3, buffer.id(), slot, offset);
}

Status RenderEncoder::bind_index_buffer(Buffer& buffer, std::size_t offset,
                                        IndexFormat format) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "index buffer belongs to another runtime");
    }
    return list_->encoder_command(5, buffer.id(), offset,
                                  static_cast<std::uint64_t>(format));
}

Status RenderEncoder::draw(std::uint32_t vertexCount,
                           std::uint32_t instanceCount,
                           std::uint32_t firstVertex,
                           std::uint32_t firstInstance) {
    (void)firstVertex;
    (void)firstInstance;
    return active_ && list_
               ? list_->encoder_command(6, {}, vertexCount, instanceCount)
               : detail::invalid_object("render encoder");
}

Status RenderEncoder::draw_indexed(std::uint32_t indexCount,
                                   std::uint32_t instanceCount,
                                   std::uint32_t firstIndex,
                                   std::int32_t vertexOffset,
                                   std::uint32_t firstInstance) {
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
    return draw(indexCount, instanceCount);
}

Status RenderEncoder::draw_indirect(Buffer& buffer, std::size_t offset,
                                    bool indexed) {
    if (auto status = bind_index_buffer(buffer, offset,
                                        indexed ? IndexFormat::uint32
                                                : IndexFormat::uint16);
        !status.ok()) {
        return status;
    }
    return draw(1, 1);
}

Status RenderEncoder::end() {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    auto status = list_->end_encoder(1);
    if (status.ok()) {
        active_ = false;
        list_ = nullptr;
    }
    return status;
}

ComputeEncoder::ComputeEncoder() noexcept = default;
ComputeEncoder::ComputeEncoder(CommandList& list) noexcept
    : list_(&list), active_(true) {}
ComputeEncoder::~ComputeEncoder() {
    if (active_ && list_ != nullptr) {
        list_->abandon_encoder(2);
    }
}
ComputeEncoder::ComputeEncoder(ComputeEncoder&& other) noexcept
    : list_(std::exchange(other.list_, nullptr)),
      active_(std::exchange(other.active_, false)) {}
ComputeEncoder& ComputeEncoder::operator=(ComputeEncoder&& other) noexcept {
    if (this != &other) {
        if (active_ && list_ != nullptr) {
            list_->abandon_encoder(2);
        }
        list_ = std::exchange(other.list_, nullptr);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Status ComputeEncoder::bind_pipeline(ComputePipeline& pipeline) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    if (!pipeline.valid() ||
        pipeline.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "compute pipeline belongs to another runtime");
    }
    return list_->encoder_command(4, pipeline.id());
}

Status ComputeEncoder::bind_storage_buffer(std::uint32_t slot, Buffer& buffer,
                                           std::size_t offset) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "storage buffer belongs to another runtime");
    }
    return list_->encoder_command(5, buffer.id(), slot, offset);
}

Status ComputeEncoder::dispatch(std::uint32_t x, std::uint32_t y,
                                std::uint32_t z) {
    if (z == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "dispatch group counts must be non-zero");
    }
    return active_ && list_
               ? list_->encoder_command(9, {}, x, y)
               : detail::invalid_object("compute encoder");
}

Status ComputeEncoder::end() {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    auto status = list_->end_encoder(2);
    if (status.ok()) {
        active_ = false;
        list_ = nullptr;
    }
    return status;
}

CopyEncoder::CopyEncoder() noexcept = default;
CopyEncoder::CopyEncoder(CommandList& list) noexcept : list_(&list), active_(true) {}
CopyEncoder::~CopyEncoder() {
    if (active_ && list_ != nullptr) {
        list_->abandon_encoder(3);
    }
}
CopyEncoder::CopyEncoder(CopyEncoder&& other) noexcept
    : list_(std::exchange(other.list_, nullptr)),
      active_(std::exchange(other.active_, false)) {}
CopyEncoder& CopyEncoder::operator=(CopyEncoder&& other) noexcept {
    if (this != &other) {
        if (active_ && list_ != nullptr) {
            list_->abandon_encoder(3);
        }
        list_ = std::exchange(other.list_, nullptr);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Status CopyEncoder::copy_buffer(Buffer& source, std::size_t sourceOffset,
                                Buffer& destination,
                                std::size_t destinationOffset,
                                std::size_t size) {
    if (!active_ || list_ == nullptr || size == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "copy requires an active encoder and non-zero size");
    }
    if (!source.valid() || !destination.valid() ||
        source.state_->runtime.get() != list_->state_->runtime.get() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "copy buffers must belong to the command-list runtime");
    }
    std::vector<std::byte> bytes(size);
    if (auto status = source.read(sourceOffset, bytes); !status.ok()) {
        return status;
    }
    if (auto status = destination.write(destinationOffset, bytes); !status.ok()) {
        return status;
    }
    if (auto status = list_->encoder_command(7, source.id(), sourceOffset, size);
        !status.ok()) {
        return status;
    }
    return list_->encoder_command(7, destination.id(), destinationOffset, size);
}

Status CopyEncoder::fill_buffer(Buffer& destination, std::size_t offset,
                                std::size_t size, std::byte value) {
    if (!active_ || list_ == nullptr || size == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "fill requires an active encoder and non-zero size");
    }
    if (!destination.valid() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "fill buffer belongs to another runtime");
    }
    std::vector<std::byte> bytes(size, value);
    if (auto status = destination.write(offset, bytes); !status.ok()) {
        return status;
    }
    return list_->encoder_command(8, destination.id(), offset, size);
}

Status CopyEncoder::end() {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("copy encoder");
    }
    auto status = list_->end_encoder(3);
    if (status.ok()) {
        active_ = false;
        list_ = nullptr;
    }
    return status;
}

QueueKind Queue::kind() const {
    const auto value = detail::payload<detail::QueuePayload>(
        state_, detail::ObjectKind::queue);
    return value ? value->kind : QueueKind::graphics;
}

Status Queue::submit(std::span<CommandList* const> commandLists,
                     Fence* signalFence, std::uint64_t signalValue) {
    if (!valid() || commandLists.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "submit requires a valid queue and command lists");
    }
    std::vector<detail::Handle> handles;
    handles.reserve(commandLists.size());
    for (auto* list : commandLists) {
        if (list == nullptr || !list->valid() ||
            list->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "submitted command list is invalid or foreign");
        }
        handles.push_back(list->state_->handle);
    }
    detail::Handle fenceHandle = 0;
    if (signalFence != nullptr) {
        if (!signalFence->valid() ||
            signalFence->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "signal fence is invalid or foreign");
        }
        fenceHandle = signalFence->state_->handle;
    }
    return state_->runtime->dispatch->submit(*state_->runtime, state_->handle,
                                             handles, fenceHandle, signalValue);
}

Status Queue::present(Swapchain& swapchain, std::uint32_t imageIndex) {
    if (!valid() || !swapchain.valid() ||
        swapchain.state_->object->runtime.get() != state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "present objects are invalid or foreign");
    }
    return state_->runtime->dispatch->present(
        *state_->runtime, state_->handle, swapchain.state_->object->handle,
        imageIndex);
}

std::uint64_t Fence::completed_value() const noexcept {
    const auto value = detail::payload<detail::FencePayload>(
        state_, detail::ObjectKind::fence);
    if (!value) {
        return 0;
    }
    std::lock_guard lock{value->mutex};
    return value->value;
}

Status Fence::wait(std::uint64_t target, std::chrono::nanoseconds timeout) {
    const auto value = detail::payload<detail::FencePayload>(
        state_, detail::ObjectKind::fence);
    if (!value) {
        return detail::invalid_object("fence");
    }
    std::unique_lock lock{value->mutex};
    if (value->changed.wait_for(lock, timeout,
                                [&] { return value->value >= target; })) {
        return Status::success();
    }
    return Status::failure(StatusCode::timeout, "fence wait timed out");
}

std::uint64_t Semaphore::value() const noexcept {
    const auto payloadValue = detail::payload<detail::SemaphorePayload>(
        state_, detail::ObjectKind::semaphore);
    return payloadValue ? payloadValue->value.load() : 0;
}

QueryPoolDesc QueryPool::desc() const {
    const auto value = detail::payload<detail::QueryPoolPayload>(
        state_, detail::ObjectKind::query_pool);
    return value ? value->desc : QueryPoolDesc{};
}

SurfaceDesc Surface::desc() const {
    const auto value = detail::payload<detail::SurfacePayload>(
        state_, detail::ObjectKind::surface);
    return value ? value->desc : SurfaceDesc{};
}

Swapchain::Swapchain() noexcept = default;
Swapchain::~Swapchain() = default;
Swapchain::Swapchain(Swapchain&&) noexcept = default;
Swapchain& Swapchain::operator=(Swapchain&&) noexcept = default;
Swapchain::Swapchain(std::unique_ptr<detail::SwapchainState> state) noexcept
    : state_(std::move(state)) {}

bool Swapchain::valid() const noexcept {
    return state_ && detail::state_valid(state_->object);
}

ObjectId Swapchain::id() const noexcept {
    return state_ ? detail::state_id(state_->object) : ObjectId{};
}

SwapchainDesc Swapchain::desc() const {
    if (!valid()) {
        return {};
    }
    const auto value = state_->object->runtime->resolve<detail::SwapchainPayload>(
        detail::ObjectKind::swapchain, state_->object->handle);
    return value ? value->desc : SwapchainDesc{};
}

AcquireResult Swapchain::acquire_next_image() {
    if (!valid()) {
        return {.status = detail::invalid_object("swapchain")};
    }
    auto& runtime = *state_->object->runtime;
    if (!runtime.config.logicalResources) {
        return {.status = detail::unsupported(runtime, "swapchain acquisition")};
    }
    const auto value = runtime.resolve<detail::SwapchainPayload>(
        detail::ObjectKind::swapchain, state_->object->handle);
    if (!value) {
        return {.status = detail::invalid_object("swapchain")};
    }
    std::lock_guard lock{value->mutex};
    if (value->acquired) {
        return {.status = Status::failure(
                    StatusCode::invalid_state,
                    "previous swapchain image has not been presented")};
    }
    const auto imageHandle = runtime.allocate(detail::ObjectKind::texture,
                                              value->image);
    state_->image = std::make_unique<Texture>(
        detail::Factory::texture(state_->object->runtime, imageHandle));
    const auto semaphoreHandle = runtime.allocate(
        detail::ObjectKind::semaphore,
        std::make_shared<detail::SemaphorePayload>(1));
    state_->available = std::make_unique<Semaphore>(
        detail::Factory::semaphore(state_->object->runtime, semaphoreHandle));
    const auto index = value->nextImage++ % value->desc.imageCount;
    value->acquired = true;
    return {
        .image = state_->image.get(),
        .imageIndex = index,
        .status = Status::success(),
        .available = state_->available.get(),
        .availableValue = 1,
    };
}

Status Swapchain::resize(Extent2D extent) {
    if (!valid()) {
        return detail::invalid_object("swapchain");
    }
    if (!validation::is_non_zero(extent)) {
        return Status::failure(StatusCode::invalid_argument,
                               "swapchain extent must be non-zero");
    }
    const auto value = state_->object->runtime->resolve<detail::SwapchainPayload>(
        detail::ObjectKind::swapchain, state_->object->handle);
    if (!value) {
        return detail::invalid_object("swapchain");
    }
    std::lock_guard lock{value->mutex};
    if (value->acquired) {
        return Status::failure(StatusCode::invalid_state,
                               "cannot resize with an acquired image");
    }
    value->desc.extent = extent;
    value->image = std::make_shared<detail::TexturePayload>(TextureDesc{
        .extent = {extent.width, extent.height, 1},
        .format = value->desc.format,
        .usage = TextureUsage::color_attachment | TextureUsage::present,
        .debugName = value->desc.debugName + " image",
    });
    state_->image.reset();
    state_->available.reset();
    return Status::success();
}

UploadRing::UploadRing() noexcept = default;
UploadRing::~UploadRing() = default;
UploadRing::UploadRing(UploadRing&&) noexcept = default;
UploadRing& UploadRing::operator=(UploadRing&&) noexcept = default;
UploadRing::UploadRing(std::unique_ptr<detail::UploadRingState> state) noexcept
    : state_(std::move(state)) {}

bool UploadRing::valid() const noexcept {
    return state_ && detail::state_valid(state_->object);
}

ObjectId UploadRing::id() const noexcept {
    return state_ ? detail::state_id(state_->object) : ObjectId{};
}

FrameAllocation UploadRing::allocate(std::size_t size, std::size_t alignment) {
    if (!valid() || size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return {};
    }
    const auto value = state_->object->runtime->resolve<detail::UploadRingPayload>(
        detail::ObjectKind::upload_ring, state_->object->handle);
    if (!value) {
        return {};
    }
    std::lock_guard lock{value->mutex};
    const auto aligned = (value->offset + alignment - 1) & ~(alignment - 1);
    if (aligned > value->bytesPerFrame || size > value->bytesPerFrame - aligned) {
        return {};
    }
    auto& frame = value->frames[value->currentFrame];
    value->offset = aligned + size;
    return {
        .buffer = &state_->buffers[value->currentFrame],
        .offset = aligned,
        .size = size,
        .mapped = frame->bytes.data() + static_cast<std::ptrdiff_t>(aligned),
    };
}

Status UploadRing::advance() {
    if (!valid()) {
        return detail::invalid_object("upload ring");
    }
    const auto value = state_->object->runtime->resolve<detail::UploadRingPayload>(
        detail::ObjectKind::upload_ring, state_->object->handle);
    if (!value) {
        return detail::invalid_object("upload ring");
    }
    std::lock_guard lock{value->mutex};
    value->currentFrame = (value->currentFrame + 1u) % value->frames.size();
    value->offset = 0;
    return Status::success();
}

} // namespace truffle::rhi
