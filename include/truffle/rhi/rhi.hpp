#pragma once

#include "truffle/core/config.hpp"
#include "truffle/core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace truffle::rhi {

// Forward declarations — descriptor structs reference these before their class definitions
class IBuffer;
class IShader;
class ITexture;
class ISampler;
class IBindGroupLayout;
class IBindGroup;
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
    unknown,
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

enum class ColorWriteFlags : std::uint32_t {
    none  = 0,
    red   = 1u << 0u,
    green = 1u << 1u,
    blue  = 1u << 2u,
    alpha = 1u << 3u,
    all   = (1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u),
};

[[nodiscard]] constexpr ColorWriteFlags operator|(
    ColorWriteFlags lhs,
    ColorWriteFlags rhs) noexcept {
    return static_cast<ColorWriteFlags>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr ColorWriteFlags operator&(
    ColorWriteFlags lhs,
    ColorWriteFlags rhs) noexcept {
    return static_cast<ColorWriteFlags>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr ColorWriteFlags& operator|=(ColorWriteFlags& lhs,
                                      ColorWriteFlags rhs) noexcept {
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

enum class ShaderStageFlags : std::uint32_t {
    none = 0,
    vertex = 1u << 0u,
    fragment = 1u << 1u,
    compute = 1u << 2u,
    graphics = (1u << 0u) | (1u << 1u),
    all = (1u << 0u) | (1u << 1u) | (1u << 2u),
};

[[nodiscard]] constexpr ShaderStageFlags operator|(
    ShaderStageFlags lhs,
    ShaderStageFlags rhs) noexcept {
    return static_cast<ShaderStageFlags>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr ShaderStageFlags operator&(
    ShaderStageFlags lhs,
    ShaderStageFlags rhs) noexcept {
    return static_cast<ShaderStageFlags>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr ShaderStageFlags& operator|=(ShaderStageFlags& lhs,
                                       ShaderStageFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool has_flag(ShaderStageFlags flags,
                                      ShaderStageFlags flag) noexcept {
    return (flags & flag) != ShaderStageFlags::none;
}

[[nodiscard]] constexpr ShaderStageFlags shader_stage_flag(
    ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::vertex:
        return ShaderStageFlags::vertex;
    case ShaderStage::fragment:
        return ShaderStageFlags::fragment;
    case ShaderStage::compute:
        return ShaderStageFlags::compute;
    }
    return ShaderStageFlags::none;
}

enum class ShaderByteFormat {
    unknown,
    contract,
    msl_source,
    spirv_binary,
    glsl_source,
    hlsl_source,
    dxil_binary,
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

enum class BindingResourceType {
    uniform_buffer,
    storage_buffer,
    sampled_texture,
    storage_texture,
    sampler,
};

enum class SamplerFilter {
    nearest,
    linear,
};

enum class SamplerMipmapMode {
    nearest,
    linear,
};

enum class SamplerAddressMode {
    repeat,
    mirrored_repeat,
    clamp_to_edge,
    clamp_to_border,
};

enum class SamplerCompareOp {
    never,
    less,
    equal,
    less_equal,
    greater,
    not_equal,
    greater_equal,
    always,
};

enum class SamplerBorderColor {
    transparent_black,
    opaque_black,
    opaque_white,
};

enum class FillMode {
    solid,
    wireframe,
};

enum class CullMode {
    none,
    front,
    back,
};

enum class FrontFace {
    counter_clockwise,
    clockwise,
};

enum class BlendFactor {
    zero,
    one,
    source_color,
    one_minus_source_color,
    destination_color,
    one_minus_destination_color,
    source_alpha,
    one_minus_source_alpha,
    destination_alpha,
    one_minus_destination_alpha,
};

enum class BlendOp {
    add,
    subtract,
    reverse_subtract,
    min,
    max,
};

struct RasterStateDesc {
    FillMode fillMode = FillMode::solid;
    CullMode cullMode = CullMode::back;
    FrontFace frontFace = FrontFace::counter_clockwise;
    bool depthClip = true;
};

struct DepthStencilStateDesc {
    SamplerCompareOp depthCompare = SamplerCompareOp::less_equal;
    bool stencilTest = false;
};

struct ColorBlendDesc {
    bool enabled = false;
    BlendFactor srcColor = BlendFactor::one;
    BlendFactor dstColor = BlendFactor::zero;
    BlendOp colorOp = BlendOp::add;
    BlendFactor srcAlpha = BlendFactor::one;
    BlendFactor dstAlpha = BlendFactor::zero;
    BlendOp alphaOp = BlendOp::add;
    ColorWriteFlags writeMask = ColorWriteFlags::all;
};

enum class VertexStepMode {
    vertex,
    instance,
};

enum class VertexFormat {
    float32,
    float32x2,
    float32x3,
    float32x4,
    uint32,
    uint32x2,
    uint32x3,
    uint32x4,
};

struct VertexBufferLayoutDesc {
    std::uint32_t binding = 0;
    std::size_t stride = 0;
    VertexStepMode stepMode = VertexStepMode::vertex;
};

struct VertexAttributeDesc {
    std::uint32_t location = 0;
    std::uint32_t binding = 0;
    VertexFormat format = VertexFormat::float32x3;
    std::size_t offset = 0;
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
    bool descriptorArrays = false;
    bool dynamicResourceIndexing = false;
    bool bindlessResources = false;
};

struct DeviceLimits {
    std::uint32_t maxTextureDimension2D = 1;
    std::size_t maxBufferSize = std::numeric_limits<std::size_t>::max();
    std::size_t minUniformBufferOffsetAlignment = 1;
    std::size_t minStorageBufferOffsetAlignment = 1;
    std::uint32_t maxColorAttachments = 1;
    std::uint32_t maxVertexBuffers = 1;
    std::uint32_t maxVertexAttributes = 16;
    std::size_t maxVertexBufferStride = 2048;
    std::uint32_t maxResourceBindings = 64;
    std::uint32_t maxDescriptorArrayElements = 1;
    std::uint32_t maxBindlessResources = 0;
    std::uint32_t maxSamplerAnisotropy = 1;
    std::uint32_t maxBindGroups = 4;
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
    std::vector<ShaderByteFormat> shaderFormats;
};

struct BackendStats {
    std::uint64_t devicesCreated = 0;
    std::uint64_t buffersCreated = 0;
    std::uint64_t texturesCreated = 0;
    std::uint64_t samplersCreated = 0;
    std::uint64_t shadersCreated = 0;
    std::uint64_t graphicsPipelinesCreated = 0;
    std::uint64_t computePipelinesCreated = 0;
    std::uint64_t bindGroupLayoutsCreated = 0;
    std::uint64_t bindGroupsCreated = 0;
    std::uint64_t surfacesCreated = 0;
    std::uint64_t swapchainsCreated = 0;
    std::uint64_t commandBuffersCreated = 0;
    std::uint64_t fencesCreated = 0;
    std::uint64_t uploadRingsCreated = 0;
    std::uint64_t drawsRecorded = 0;
    std::uint64_t dispatchesRecorded = 0;
    std::uint64_t submissions = 0;
    std::uint64_t debugLabelsPushed = 0;
    std::uint64_t debugMarkersInserted = 0;
};

enum class BackendEventKind {
    device_created,
    resource_created,
    pipeline_created,
    bind_group_created,
    surface_created,
    swapchain_created,
    command_buffer_created,
    fence_created,
    upload_ring_created,
    command_recorded,
    debug_marker,
    submitted,
};

struct BackendEvent {
    std::uint64_t sequence = 0;
    BackendKind backend = BackendKind::null_backend;
    BackendEventKind kind = BackendEventKind::resource_created;
    core::StatusCode status = core::StatusCode::ok;
    std::string label;
    std::string message;
};

struct BackendParityReport {
    BackendKind backend = BackendKind::null_backend;
    std::size_t adapterCount = 0;
    bool graphicsQueue = false;
    bool computeQueue = false;
    bool transferQueue = false;
    bool presentation = false;
    bool nativeSurface = false;
    bool shaderReflection = false;
    bool debugLabels = false;
    std::uint32_t maxFramesInFlight = 0;
    std::uint32_t maxResourceBindings = 0;
    std::uint32_t maxVertexAttributes = 0;
    std::size_t maxVertexBufferStride = 0;
    std::size_t formatCount = 0;
    std::size_t shaderFormatCount = 0;
    BackendStats stats;
    bool descriptorArrays = false;
    bool dynamicResourceIndexing = false;
    bool bindlessResources = false;
    std::uint32_t maxDescriptorArrayElements = 0;
    std::uint32_t maxBindlessResources = 0;
    std::uint32_t maxSamplerAnisotropy = 0;
    bool unifiedMemory = false;
    std::size_t memoryHeapCount = 0;
    std::uint64_t memoryBudgetBytes = 0;
    bool dedicatedMemoryHeap = false;
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

[[nodiscard]] inline bool supports_shader_byte_format(
    const Capabilities& capabilities,
    ShaderByteFormat format) noexcept {
    if (format == ShaderByteFormat::unknown) {
        return true;
    }
    for (const auto supported : capabilities.shaderFormats) {
        if (supported == format) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool supports_descriptor_arrays(
    const Capabilities& capabilities) noexcept {
    return capabilities.features.descriptorArrays &&
           capabilities.limits.maxDescriptorArrayElements > 1;
}

[[nodiscard]] constexpr bool supports_dynamic_resource_indexing(
    const Capabilities& capabilities) noexcept {
    return supports_descriptor_arrays(capabilities) &&
           capabilities.features.dynamicResourceIndexing;
}

[[nodiscard]] constexpr bool supports_bindless_resources(
    const Capabilities& capabilities) noexcept {
    return supports_dynamic_resource_indexing(capabilities) &&
           capabilities.features.bindlessResources &&
           capabilities.limits.maxBindlessResources > 1;
}

[[nodiscard]] constexpr bool has_flag(BufferUsageFlags flags,
                                      BufferUsageFlags flag) noexcept {
    return (flags & flag) != BufferUsageFlags::none;
}

[[nodiscard]] constexpr bool has_flag(TextureUsageFlags flags,
                                      TextureUsageFlags flag) noexcept {
    return (flags & flag) != TextureUsageFlags::none;
}

[[nodiscard]] constexpr bool has_flag(ColorWriteFlags flags,
                                      ColorWriteFlags flag) noexcept {
    return (flags & flag) != ColorWriteFlags::none;
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
    std::optional<SamplerFilter> minFilter;
    std::optional<SamplerFilter> magFilter;
    std::optional<SamplerMipmapMode> mipmapMode;
    SamplerAddressMode addressModeU = SamplerAddressMode::clamp_to_edge;
    SamplerAddressMode addressModeV = SamplerAddressMode::clamp_to_edge;
    SamplerAddressMode addressModeW = SamplerAddressMode::clamp_to_edge;
    float mipLodBias = 0.0f;
    float minLod = 0.0f;
    float maxLod = 32.0f;
    std::uint32_t maxAnisotropy = 1;
    bool compareEnabled = false;
    SamplerCompareOp compareOp = SamplerCompareOp::less_equal;
    SamplerBorderColor borderColor = SamplerBorderColor::opaque_black;
    std::string debugName;
};

[[nodiscard]] inline SamplerFilter effective_min_filter(
    const SamplerDesc& desc) noexcept {
    return desc.minFilter.value_or(desc.linear_filtering ? SamplerFilter::linear
                                                         : SamplerFilter::nearest);
}

[[nodiscard]] inline SamplerFilter effective_mag_filter(
    const SamplerDesc& desc) noexcept {
    return desc.magFilter.value_or(desc.linear_filtering ? SamplerFilter::linear
                                                         : SamplerFilter::nearest);
}

[[nodiscard]] inline SamplerMipmapMode effective_mipmap_mode(
    const SamplerDesc& desc) noexcept {
    return desc.mipmapMode.value_or(desc.linear_filtering
                                        ? SamplerMipmapMode::linear
                                        : SamplerMipmapMode::nearest);
}

struct ShaderDesc {
    ShaderStage            stage      = ShaderStage::vertex;
    ShaderByteFormat       byteFormat = ShaderByteFormat::unknown;
    std::string            entryPoint = "main";
    std::vector<std::byte> bytecode;
};

struct BindingLayoutDesc {
    std::uint32_t bindingIndex = 0;
    BindingResourceType type = BindingResourceType::uniform_buffer;
    ShaderStageFlags visibility = ShaderStageFlags::all;
    std::uint32_t arrayCount = 1;
    std::size_t minBindingSize = 0;
    bool dynamicIndexing = false;
    bool bindless = false;
    std::uint32_t groupIndex = 0;
    bool dynamicOffset = false;
};

struct PipelineLayoutDesc {
    std::string debugName;
    std::vector<BindingLayoutDesc> bindings;
};

struct BufferBindingDesc {
    IBuffer* buffer = nullptr;
    std::size_t offset = 0;
    std::size_t size = 0; // 0 means from offset to end of buffer.
};

struct BindGroupEntry {
    std::uint32_t bindingIndex = 0;
    BindingResourceType type = BindingResourceType::uniform_buffer;
    BufferBindingDesc buffer;
    ITexture* texture = nullptr;
    ISampler* sampler = nullptr;
    std::vector<BufferBindingDesc> buffers;
    std::vector<ITexture*> textures;
    std::vector<ISampler*> samplers;
};

struct BindGroupDynamicOffset {
    std::uint32_t bindingIndex = 0;
    std::uint32_t arrayElement = 0;
    std::size_t offset = 0;
};

struct BindGroupLayoutDesc {
    std::string debugName;
    std::vector<BindingLayoutDesc> bindings;
};

struct BindGroupDesc {
    std::string debugName;
    IBindGroupLayout* layout = nullptr;
    std::vector<BindGroupEntry> entries;
};

struct PipelineDesc {
    std::string       debugName;
    std::uint64_t     cacheKey       = 0;
    IShader*          vertexShader   = nullptr;
    IShader*          fragmentShader = nullptr;
    PipelineLayoutDesc layout;
    PrimitiveTopology topology       = PrimitiveTopology::triangle_list;
    TextureFormat     colorFormat    = TextureFormat::bgra8_unorm;
    TextureFormat     depthFormat    = TextureFormat::unknown;
    bool              depthTest      = false;
    bool              depthWrite     = false;
    RasterStateDesc   rasterState;
    DepthStencilStateDesc depthStencilState;
    ColorBlendDesc    colorBlend;
    std::vector<VertexBufferLayoutDesc> vertexBuffers;
    std::vector<VertexAttributeDesc> vertexAttributes;
};

struct ComputePipelineDesc {
    std::string debugName;
    std::uint64_t cacheKey = 0;
    IShader*    computeShader = nullptr;
    PipelineLayoutDesc layout;
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

struct DebugLabelDesc {
    std::string name;
    bool hasColor = false;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
};

// ---------------------------------------------------------------------------
// Render pass descriptors
// ---------------------------------------------------------------------------

struct ClearColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

struct ColorAttachmentDesc {
    // nullptr means no concrete color attachment, primarily for headless/null paths.
    // Swapchain rendering should pass the texture returned by acquire_next_texture().
    ITexture*  texture    = nullptr;
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
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const BufferDesc& desc() const noexcept = 0;
};

class ITexture {
public:
    virtual ~ITexture() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const TextureDesc& desc() const noexcept = 0;
};

class ISampler {
public:
    virtual ~ISampler() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const SamplerDesc& desc() const noexcept {
        static const SamplerDesc defaultDesc{};
        return defaultDesc;
    }
};

class IBindGroupLayout {
public:
    virtual ~IBindGroupLayout() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const BindGroupLayoutDesc& desc() const noexcept = 0;
};

class IBindGroup {
public:
    virtual ~IBindGroup() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const BindGroupDesc& desc() const noexcept = 0;
};

class IShader {
public:
    virtual ~IShader() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
};

class IPipelineReflection;

class IPipeline {
public:
    virtual ~IPipeline() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const PipelineDesc& desc() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t cache_key() const noexcept {
        return desc().cacheKey;
    }
    
    /// Optional: returns reflection metadata, or nullptr if unavailable.
    [[nodiscard]] virtual const IPipelineReflection* reflection() const noexcept = 0;
};

class IComputePipeline {
public:
    virtual ~IComputePipeline() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const ComputePipelineDesc& desc() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t cache_key() const noexcept {
        return desc().cacheKey;
    }

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

    // Debug labels and markers are recording-time diagnostics. Labels must be
    // balanced before end(); markers are instantaneous breadcrumbs.
    [[nodiscard]] virtual core::Status push_debug_label(
        const DebugLabelDesc& desc) = 0;
    [[nodiscard]] virtual core::Status pop_debug_label() = 0;
    [[nodiscard]] virtual core::Status insert_debug_marker(
        const DebugLabelDesc& desc) = 0;

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
    [[nodiscard]] virtual core::Status bind_group(
        std::uint32_t groupIndex, IBindGroup& group) = 0;
    // Optional dynamic buffer offsets are supplied at command binding time and
    // are added to the static buffer offsets stored in the bind group.
    [[nodiscard]] virtual core::Status bind_group(
        std::uint32_t groupIndex,
        IBindGroup& group,
        const std::vector<BindGroupDynamicOffset>& dynamicOffsets) {
        if (!dynamicOffsets.empty()) {
            return core::Status::failure(
                core::StatusCode::unsupported,
                "dynamic bind group offsets are not supported by this command buffer");
        }
        return bind_group(groupIndex, group);
    }
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
    [[nodiscard]] virtual core::Result<std::unique_ptr<IBindGroupLayout>>
    create_bind_group_layout(const BindGroupLayoutDesc& desc) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<IBindGroup>>
    create_bind_group(const BindGroupDesc& desc) = 0;
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
    [[nodiscard]] virtual BackendStats backend_stats() const noexcept {
        return {};
    }
    [[nodiscard]] virtual std::vector<BackendEvent> recent_events() const {
        return {};
    }
    virtual void clear_diagnostics() noexcept {}
    [[nodiscard]] virtual std::vector<AdapterInfo> enumerate_adapters() const = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<IDevice>>
    create_device(const DeviceDesc& desc) = 0;
};

[[nodiscard]] inline BackendParityReport collect_backend_parity_report(
    const IBackend& backend) {
    BackendParityReport report;
    report.backend = backend.kind();
    report.stats = backend.backend_stats();

    const auto adapters = backend.enumerate_adapters();
    report.adapterCount = adapters.size();
    if (adapters.empty()) {
        return report;
    }

    const auto& caps = adapters.front().capabilities;
    report.graphicsQueue = caps.queues.graphics;
    report.computeQueue = caps.queues.compute;
    report.transferQueue = caps.queues.transfer;
    report.presentation = caps.features.presentation;
    report.nativeSurface = caps.features.nativeSurface;
    report.shaderReflection = caps.features.shaderReflection;
    report.debugLabels = caps.features.debugLabels;
    report.descriptorArrays = supports_descriptor_arrays(caps);
    report.dynamicResourceIndexing = supports_dynamic_resource_indexing(caps);
    report.bindlessResources = supports_bindless_resources(caps);
    report.unifiedMemory = caps.features.unifiedMemory;
    report.maxFramesInFlight = caps.maxFramesInFlight;
    report.maxResourceBindings = caps.limits.maxResourceBindings;
    report.maxVertexAttributes = caps.limits.maxVertexAttributes;
    report.maxVertexBufferStride = caps.limits.maxVertexBufferStride;
    report.maxDescriptorArrayElements = caps.limits.maxDescriptorArrayElements;
    report.maxBindlessResources = caps.limits.maxBindlessResources;
    report.maxSamplerAnisotropy = caps.limits.maxSamplerAnisotropy;
    report.formatCount = caps.formats.size();
    report.shaderFormatCount = caps.shaderFormats.size();
    report.memoryHeapCount = caps.memoryHeaps.size();
    for (const auto& heap : caps.memoryHeaps) {
        report.memoryBudgetBytes += heap.budgetBytes;
        report.dedicatedMemoryHeap = report.dedicatedMemoryHeap || heap.dedicated;
    }
    return report;
}

} // namespace truffle::rhi
