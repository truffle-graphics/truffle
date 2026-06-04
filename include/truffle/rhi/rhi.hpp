#pragma once

#include "truffle/core/config.hpp"
#include "truffle/core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace truffle::rhi {

// Forward declarations — descriptor structs reference these before their class definitions
class IBuffer;
class IShader;
class ITexture;
class ICommandBuffer; // used in ISwapchain::schedule_present

enum class BackendKind {
    null_backend,
    vulkan,
    direct3d,
    opengl,
    metal,
};

enum class QueueKind {
    graphics,
    compute,
    transfer,
};

enum class BufferUsage {
    vertex,
    index,
    uniform,
    storage,
    transfer_source,
    transfer_destination,
    indirect,
};

enum class BufferUsageFlags : std::uint32_t {
    none                 = 0,
    vertex               = 1u << 0u,
    index                = 1u << 1u,
    uniform              = 1u << 2u,
    storage              = 1u << 3u,
    transfer_source      = 1u << 4u,
    transfer_destination = 1u << 5u,
    indirect             = 1u << 6u,
};

[[nodiscard]] constexpr BufferUsageFlags operator|(
    BufferUsageFlags lhs,
    BufferUsageFlags rhs) noexcept {
    return static_cast<BufferUsageFlags>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr BufferUsageFlags operator&(
    BufferUsageFlags lhs,
    BufferUsageFlags rhs) noexcept {
    return static_cast<BufferUsageFlags>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr BufferUsageFlags& operator|=(BufferUsageFlags& lhs,
                                       BufferUsageFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

enum class IndexFormat {
    uint16,
    uint32,
};

enum class TextureFormat {
    rgba8_unorm,
    bgra8_unorm,
    depth32_float,
};

enum class TextureUsageFlags : std::uint32_t {
    none                 = 0,
    sampled              = 1u << 0u,
    color_attachment     = 1u << 1u,
    depth_stencil        = 1u << 2u,
    storage              = 1u << 3u,
    transfer_source      = 1u << 4u,
    transfer_destination = 1u << 5u,
};

[[nodiscard]] constexpr TextureUsageFlags operator|(
    TextureUsageFlags lhs,
    TextureUsageFlags rhs) noexcept {
    return static_cast<TextureUsageFlags>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr TextureUsageFlags operator&(
    TextureUsageFlags lhs,
    TextureUsageFlags rhs) noexcept {
    return static_cast<TextureUsageFlags>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr TextureUsageFlags& operator|=(TextureUsageFlags& lhs,
                                        TextureUsageFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

enum class NativeSurfaceKind {
    headless,
    win32,
    wayland,
    xcb,
    cocoa_layer,
    external,
};

enum class PresentMode {
    immediate,
    fifo,
    mailbox,
};

enum class ShaderStage {
    vertex,
    fragment,
    compute,
};

enum class PrimitiveTopology {
    triangle_list,
    triangle_strip,
    line_list,
    point_list,
};

enum class LoadOp  { load, clear, dont_care };
enum class StoreOp { store, dont_care };

enum class TextureDimension {
    one_d,
    two_d,
    three_d,
    cube,
};

enum class MemoryDomain {
    automatic,
    device_local,
    upload,
    readback,
};

enum class AdapterType {
    unknown,
    integrated_gpu,
    discrete_gpu,
    virtual_gpu,
    cpu,
};

enum class MemoryHeapKind {
    device_local,
    host_visible,
    unified,
};

enum class CommandBufferState {
    initial,
    recording,
    executable,
    submitted,
};

enum class ResourceState {
    undefined,
    copy_source,
    copy_destination,
    shader_read,
    storage_read_write,
    color_attachment,
    depth_attachment,
    present,
};

struct QueueCapabilities {
    bool graphics = false;
    bool compute  = false;
    bool transfer = false;
};

struct FeatureSupport {
    bool headlessSurface = false;
    bool nativeSurface   = false;
    bool presentation    = false;
    bool compute         = false;
    bool indirectDraw    = false;
    bool shaderReflection = false;
    bool debugLabels     = false;
    bool validation      = false;
    bool unifiedMemory   = false;
};

struct DeviceLimits {
    std::uint32_t maxTextureDimension2D = 1;
    std::size_t maxBufferSize = std::numeric_limits<std::size_t>::max();
    std::size_t minUniformBufferOffsetAlignment = 1;
    std::size_t minStorageBufferOffsetAlignment = 1;
    std::uint32_t maxColorAttachments = 1;
    std::uint32_t maxVertexBuffers = 1;
};

struct FormatSupport {
    TextureFormat format = TextureFormat::rgba8_unorm;
    bool sampled = false;
    bool colorAttachment = false;
    bool depthStencilAttachment = false;
    bool storageTexture = false;
    bool transferSource = false;
    bool transferDestination = false;
};

struct MemoryHeapInfo {
    MemoryHeapKind kind = MemoryHeapKind::device_local;
    std::uint64_t budgetBytes = 0;
    bool dedicated = false;
};

struct Capabilities {
    bool presentation = false;
    bool validation = false;
    std::uint32_t maxFramesInFlight = 1;
    QueueCapabilities queues;
    FeatureSupport features;
    DeviceLimits limits;
    std::vector<FormatSupport> formats;
    std::vector<MemoryHeapInfo> memoryHeaps;
    std::vector<PresentMode> presentModes;
    std::vector<NativeSurfaceKind> surfaceKinds;
};

struct AdapterInfo {
    std::uint32_t id = 0;
    std::string name;
    BackendKind backend = BackendKind::null_backend;
    Capabilities capabilities;
    AdapterType type = AdapterType::unknown;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::string driverDescription;
};

[[nodiscard]] inline bool supports_queue(const Capabilities& capabilities,
                                         QueueKind queue) noexcept {
    switch (queue) {
        case QueueKind::graphics: return capabilities.queues.graphics;
        case QueueKind::compute: return capabilities.queues.compute;
        case QueueKind::transfer: return capabilities.queues.transfer;
    }
    return false;
}

[[nodiscard]] inline const FormatSupport* find_format_support(
    const Capabilities& capabilities,
    TextureFormat format) noexcept {
    for (const auto& support : capabilities.formats) {
        if (support.format == format) {
            return &support;
        }
    }
    return nullptr;
}

[[nodiscard]] inline bool supports_texture_format(
    const Capabilities& capabilities,
    TextureFormat format) noexcept {
    return find_format_support(capabilities, format) != nullptr;
}

[[nodiscard]] inline bool supports_present_mode(
    const Capabilities& capabilities,
    PresentMode mode) noexcept {
    for (const auto supported : capabilities.presentModes) {
        if (supported == mode) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool supports_native_surface_kind(
    const Capabilities& capabilities,
    NativeSurfaceKind kind) noexcept {
    for (const auto supported : capabilities.surfaceKinds) {
        if (supported == kind) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool has_flag(BufferUsageFlags flags,
                                      BufferUsageFlags flag) noexcept {
    return (flags & flag) != BufferUsageFlags::none;
}

[[nodiscard]] constexpr bool has_flag(TextureUsageFlags flags,
                                      TextureUsageFlags flag) noexcept {
    return (flags & flag) != TextureUsageFlags::none;
}

[[nodiscard]] constexpr BufferUsageFlags to_buffer_usage_flags(
    BufferUsage usage) noexcept {
    switch (usage) {
        case BufferUsage::vertex: return BufferUsageFlags::vertex;
        case BufferUsage::index: return BufferUsageFlags::index;
        case BufferUsage::uniform: return BufferUsageFlags::uniform;
        case BufferUsage::storage: return BufferUsageFlags::storage;
        case BufferUsage::transfer_source: return BufferUsageFlags::transfer_source;
        case BufferUsage::transfer_destination:
            return BufferUsageFlags::transfer_destination;
        case BufferUsage::indirect: return BufferUsageFlags::indirect;
    }
    return BufferUsageFlags::none;
}

struct DeviceDesc {
    std::uint32_t adapterId = 0;
    core::RuntimeConfig runtime;
};

struct Extent2D {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
};

struct BufferDesc {
    std::size_t size = 0;
    BufferUsage usage = BufferUsage::vertex;
    BufferUsageFlags usageFlags = BufferUsageFlags::none;
    MemoryDomain memory = MemoryDomain::automatic;
    bool mappedAtCreation = false;
    std::string debugName;
};

struct TextureDesc {
    Extent2D extent;
    std::uint32_t depth = 1;
    TextureFormat format = TextureFormat::rgba8_unorm;
    TextureDimension dimension = TextureDimension::two_d;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    std::uint32_t sampleCount = 1;
    TextureUsageFlags usageFlags = TextureUsageFlags::none;
    MemoryDomain memory = MemoryDomain::automatic;
    std::string debugName;
};

[[nodiscard]] constexpr BufferUsageFlags effective_buffer_usage(
    const BufferDesc& desc) noexcept {
    return desc.usageFlags == BufferUsageFlags::none
        ? to_buffer_usage_flags(desc.usage)
        : desc.usageFlags;
}

[[nodiscard]] constexpr TextureUsageFlags default_texture_usage(
    TextureFormat format) noexcept {
    if (format == TextureFormat::depth32_float) {
        return TextureUsageFlags::sampled | TextureUsageFlags::depth_stencil;
    }
    return TextureUsageFlags::sampled | TextureUsageFlags::color_attachment;
}

[[nodiscard]] constexpr TextureUsageFlags effective_texture_usage(
    const TextureDesc& desc) noexcept {
    return desc.usageFlags == TextureUsageFlags::none
        ? default_texture_usage(desc.format)
        : desc.usageFlags;
}

struct BufferViewDesc {
    IBuffer* buffer = nullptr;
    std::size_t offset = 0;
    std::size_t size = 0; // 0 means from offset to end of buffer.
    BufferUsageFlags requiredUsage = BufferUsageFlags::none;
};

struct TextureSubresourceRange {
    std::uint32_t baseMipLevel = 0;
    std::uint32_t mipLevelCount = 1;
    std::uint32_t baseArrayLayer = 0;
    std::uint32_t arrayLayerCount = 1;
};

struct TextureViewDesc {
    ITexture* texture = nullptr;
    TextureFormat format = TextureFormat::rgba8_unorm;
    TextureDimension dimension = TextureDimension::two_d;
    TextureSubresourceRange range;
    TextureUsageFlags requiredUsage = TextureUsageFlags::sampled;
};

struct BufferBarrierDesc {
    IBuffer* buffer = nullptr;
    ResourceState before = ResourceState::undefined;
    ResourceState after = ResourceState::undefined;
};

struct TextureBarrierDesc {
    ITexture* texture = nullptr;
    ResourceState before = ResourceState::undefined;
    ResourceState after = ResourceState::undefined;
    TextureSubresourceRange range;
};

struct SamplerDesc {
    bool linear_filtering = true;
};

struct ShaderDesc {
    ShaderStage            stage      = ShaderStage::vertex;
    std::string            entryPoint = "main";
    std::vector<std::byte> bytecode;
};

struct PipelineDesc {
    std::string       debugName;
    IShader*          vertexShader   = nullptr;
    IShader*          fragmentShader = nullptr;
    PrimitiveTopology topology       = PrimitiveTopology::triangle_list;
    TextureFormat     colorFormat    = TextureFormat::bgra8_unorm;
    bool              depthTest      = true;
    bool              depthWrite     = true;
};

struct ComputePipelineDesc {
    std::string debugName;
    IShader*    computeShader = nullptr;
};

struct NativeSurface {
    NativeSurfaceKind kind = NativeSurfaceKind::headless;
    void* handle = nullptr;
    void* display = nullptr;
};

struct SurfaceDesc {
    NativeSurface native;
    Extent2D initialExtent;
};

struct SwapchainDesc {
    Extent2D extent;
    TextureFormat format = TextureFormat::bgra8_unorm;
    std::uint32_t framesInFlight = 2;
    std::uint32_t imageCount = 0;
    PresentMode presentMode = PresentMode::fifo;
};

[[nodiscard]] constexpr std::uint32_t effective_swapchain_image_count(
    const SwapchainDesc& desc) noexcept {
    return desc.imageCount != 0 ? desc.imageCount : desc.framesInFlight;
}

struct SwapchainAcquireResult {
    core::Status status = core::Status::success();
    ITexture* texture = nullptr;
    std::uint32_t imageIndex = 0;
    bool suboptimal = false;
    bool outOfDate = false;

    [[nodiscard]] bool ok() const noexcept {
        return status.ok() && texture != nullptr && !outOfDate;
    }
};

struct FenceDesc {
    bool signaled = false;
    std::uint64_t initialValue = 0;
};

// ---------------------------------------------------------------------------
// Render pass descriptors
// ---------------------------------------------------------------------------

struct ClearColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

struct ColorAttachmentDesc {
    ITexture*  texture    = nullptr;         // nullptr = swapchain drawable
    LoadOp     loadOp     = LoadOp::clear;
    StoreOp    storeOp    = StoreOp::store;
    ClearColor clearValue = {};
};

struct DepthAttachmentDesc {
    ITexture* texture    = nullptr;          // nullptr = no depth attachment
    LoadOp    loadOp     = LoadOp::clear;
    StoreOp   storeOp    = StoreOp::dont_care;
    float     clearDepth = 1.0f;
};

struct RenderPassDesc {
    Extent2D             extent;
    ColorAttachmentDesc  colorAttachment;
    DepthAttachmentDesc  depthAttachment;    // depthAttachment.texture==nullptr → no depth
};

class IBuffer {
public:
    virtual ~IBuffer() = default;
    [[nodiscard]] virtual const BufferDesc& desc() const noexcept = 0;
};

class ITexture {
public:
    virtual ~ITexture() = default;
    [[nodiscard]] virtual const TextureDesc& desc() const noexcept = 0;
};

class ISampler {
public:
    virtual ~ISampler() = default;
};

class IShader {
public:
    virtual ~IShader() = default;
};

class IPipelineReflection;

class IPipeline {
public:
    virtual ~IPipeline() = default;
    [[nodiscard]] virtual const PipelineDesc& desc() const noexcept = 0;
    
    /// Optional: returns reflection metadata, or nullptr if unavailable.
    [[nodiscard]] virtual const IPipelineReflection* reflection() const noexcept = 0;
};

class IComputePipeline {
public:
    virtual ~IComputePipeline() = default;
    [[nodiscard]] virtual const ComputePipelineDesc& desc() const noexcept = 0;

    /// Optional: returns reflection metadata, or nullptr if unavailable.
    [[nodiscard]] virtual const IPipelineReflection* reflection() const noexcept = 0;
};

class ISurface {
public:
    virtual ~ISurface() = default;
    [[nodiscard]] virtual const SurfaceDesc& desc() const noexcept = 0;
};

class ISwapchain {
public:
    virtual ~ISwapchain() = default;
    [[nodiscard]] virtual const SwapchainDesc& desc() const noexcept = 0;
    [[nodiscard]] virtual core::Status resize(Extent2D extent) = 0;
    [[nodiscard]] virtual std::uint32_t image_count() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t current_image_index() const noexcept = 0;
    [[nodiscard]] virtual bool has_acquired_texture() const noexcept = 0;
    // Rich acquire path: reports drawable availability, image index, and
    // backend-specific suboptimal/out-of-date presentation state.
    [[nodiscard]] virtual SwapchainAcquireResult acquire_next_texture_result() = 0;
    // Compatibility wrapper for the rich acquire path. Call once per frame
    // before begin_render_pass. Returns nullptr when acquisition fails.
    [[nodiscard]] virtual ITexture* acquire_next_texture() {
        return acquire_next_texture_result().texture;
    }
    // Schedule presentation of the current drawable via the given command buffer.
    // Must be called while the command buffer is recording, after
    // end_render_pass() and before end(), after a successful acquire. Headless
    // swapchains validate the sequence and then no-op.
    [[nodiscard]] virtual core::Status schedule_present(ICommandBuffer& cmd) = 0;
};

class IFence {
public:
    virtual ~IFence() = default;
    [[nodiscard]] virtual bool signaled() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t value() const noexcept = 0;
    // Wait up to timeoutNanoseconds. Returns StatusCode::timeout if the fence is
    // still unsignaled when the timeout expires.
    [[nodiscard]] virtual core::Status wait_for(
        std::uint64_t timeoutNanoseconds) noexcept = 0;
    // Timeline-style wait. Value 0 is always considered reached.
    [[nodiscard]] virtual core::Status wait_for_value(
        std::uint64_t targetValue,
        std::uint64_t timeoutNanoseconds) noexcept = 0;
    // Reset the fence to the unsignaled state when backend rules allow it.
    [[nodiscard]] virtual core::Status reset() noexcept = 0;
    // Block until the fence is signaled. Returns immediately if already signaled.
    virtual void wait() noexcept = 0;
};

// ---------------------------------------------------------------------------
// Frame upload ring — N-buffered CPU-writable GPU-visible upload memory
// ---------------------------------------------------------------------------

struct FrameAllocation {
    IBuffer*    buffer    = nullptr; // backing buffer; nullptr signals exhaustion
    std::size_t offset    = 0;
    void*       mappedPtr = nullptr; // CPU-writable pointer into the buffer
    std::size_t size      = 0;

    [[nodiscard]] bool valid() const noexcept { return buffer != nullptr; }
};

class IFrameUploadRing {
public:
    virtual ~IFrameUploadRing() = default;

    // Allocate size bytes aligned to alignment from the current frame's range.
    // Returns an invalid FrameAllocation (buffer == nullptr) if the ring is full.
    [[nodiscard]] virtual FrameAllocation allocate(
        std::size_t size, std::size_t alignment = 16) = 0;

    // Advance to the next frame slot, reclaiming the oldest completed frame.
    // Call once per frame after verifying prior GPU work is complete.
    virtual void advance() = 0;
    // Advance only if the provided completion fence is signaled; otherwise
    // returns StatusCode::timeout and leaves the current frame unchanged.
    [[nodiscard]] virtual core::Status advance_if_ready(
        const IFence& completedFence) = 0;

    [[nodiscard]] virtual std::uint32_t frames_in_flight() const noexcept = 0;
    [[nodiscard]] virtual std::size_t   capacity_per_frame() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t current_frame_index() const noexcept = 0;
};

class ICommandBuffer {
public:
    virtual ~ICommandBuffer() = default;

    // Command buffers are one-shot while owned by the caller. Backend pools may
    // recycle storage after destruction, but a submitted command buffer cannot be
    // re-recorded or submitted again through the same handle.
    [[nodiscard]] virtual core::Status begin() = 0;
    [[nodiscard]] virtual core::Status end() = 0;
    [[nodiscard]] virtual CommandBufferState state() const noexcept = 0;
    [[nodiscard]] virtual bool ready_for_submit() const noexcept = 0;

    // Render pass
    [[nodiscard]] virtual core::Status begin_render_pass(
        const RenderPassDesc& desc) = 0;
    [[nodiscard]] virtual core::Status end_render_pass() = 0;

    // Resource binding
    [[nodiscard]] virtual core::Status bind_pipeline(IPipeline& pipeline) = 0;
    [[nodiscard]] virtual core::Status bind_compute_pipeline(IComputePipeline& pipeline) = 0;
    [[nodiscard]] virtual core::Status bind_vertex_buffer(
        std::uint32_t binding, IBuffer& buffer,
        std::size_t offset = 0) = 0;
    [[nodiscard]] virtual core::Status bind_index_buffer(
        IBuffer& buffer, std::size_t offset = 0,
        IndexFormat format = IndexFormat::uint32) = 0;
    // Bind a uniform/constant buffer to both vertex and fragment stages.
    [[nodiscard]] virtual core::Status bind_uniform_buffer(
        std::uint32_t binding, IBuffer& buffer,
        std::size_t offset = 0) = 0;
    // Bind a storage buffer for compute reading/writing.
    [[nodiscard]] virtual core::Status bind_storage_buffer(
        std::uint32_t binding, IBuffer& buffer,
        std::size_t offset = 0) = 0;
    [[nodiscard]] virtual core::Status resource_barrier(
        const BufferBarrierDesc& barrier) = 0;
    [[nodiscard]] virtual core::Status resource_barrier(
        const TextureBarrierDesc& barrier) = 0;
    [[nodiscard]] virtual core::Status set_viewport(
        float x, float y, float width, float height,
        float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
    [[nodiscard]] virtual core::Status set_scissor(
        std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height) = 0;

    // Draw calls
    [[nodiscard]] virtual core::Status draw(
        std::uint32_t vertex_count) = 0;
    [[nodiscard]] virtual core::Status draw_instanced(
        std::uint32_t vertex_count,
        std::uint32_t instance_count) = 0;
    [[nodiscard]] virtual core::Status draw_indexed(
        std::uint32_t index_count) = 0;
    [[nodiscard]] virtual core::Status draw_indexed_instanced(
        std::uint32_t index_count,
        std::uint32_t instance_count) = 0;
    [[nodiscard]] virtual core::Status draw_indirect(
        IBuffer& indirect_buffer, std::size_t offset) = 0;
    [[nodiscard]] virtual core::Status draw_indexed_indirect(
        IBuffer& indirect_buffer, std::size_t offset) = 0;

    // Compute calls
    [[nodiscard]] virtual core::Status dispatch_compute(
        std::uint32_t group_count_x,
        std::uint32_t group_count_y,
        std::uint32_t group_count_z) = 0;
};

class IQueue {
public:
    virtual ~IQueue() = default;
    [[nodiscard]] virtual QueueKind kind() const noexcept = 0;
    [[nodiscard]] virtual core::Status submit(ICommandBuffer& command_buffer,
                                              IFence* signal_fence = nullptr) = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;

    [[nodiscard]] virtual const Capabilities& capabilities() const noexcept = 0;
    [[nodiscard]] virtual IQueue& queue(QueueKind kind) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<IBuffer>>
    create_buffer(const BufferDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<ITexture>>
    create_texture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<ISampler>>
    create_sampler(const SamplerDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<IShader>>
    create_shader(const ShaderDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<IPipeline>>
    create_pipeline(const PipelineDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<IComputePipeline>>
    create_compute_pipeline(const ComputePipelineDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<ISurface>>
    create_surface(const SurfaceDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<ISwapchain>>
    create_swapchain(ISurface& surface, const SwapchainDesc& desc) = 0;

    using CommandBufferDeleter = void (*)(ICommandBuffer*);
    using CommandBufferPtr = std::unique_ptr<ICommandBuffer, CommandBufferDeleter>;
    [[nodiscard]] virtual CommandBufferPtr create_command_buffer() = 0;

    using FenceDeleter = void (*)(IFence*);
    using FencePtr = std::unique_ptr<IFence, FenceDeleter>;
    [[nodiscard]] virtual FencePtr create_fence(const FenceDesc& desc) = 0;

    [[nodiscard]] virtual core::Result<std::unique_ptr<IFrameUploadRing>>
    create_upload_ring(std::uint32_t frames_in_flight,
                       std::size_t   capacity_per_frame) = 0;
};

class IBackend {
public:
    virtual ~IBackend() = default;

    [[nodiscard]] virtual BackendKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::vector<AdapterInfo> enumerate_adapters() const = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<IDevice>>
    create_device(const DeviceDesc& desc) = 0;
};

} // namespace truffle::rhi
