#pragma once

#include "truffle/core/config.hpp"
#include "truffle/core/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
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
    depth32_float_stencil8,
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

enum class NativeDescriptorMappingModel {
    direct_slots,
    descriptor_sets,
    descriptor_tables,
    argument_buffer,
};

enum class NativeDescriptorAllocationModel {
    inline_direct,
    bind_group_owned,
    pooled,
};

enum class NativeDescriptorUpdateModel {
    direct_write,
    copy_into_allocation,
    rebuild_allocation,
};

enum class NativeDescriptorBudgetModel {
    native_slot_spans,
    descriptor_count,
    bind_group_count,
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

enum class StencilOp {
    keep,
    zero,
    replace,
    increment_clamp,
    decrement_clamp,
    invert,
    increment_wrap,
    decrement_wrap,
};

struct RasterStateDesc {
    FillMode fillMode = FillMode::solid;
    CullMode cullMode = CullMode::back;
    FrontFace frontFace = FrontFace::counter_clockwise;
    bool depthClip = true;
};

struct StencilFaceStateDesc {
    SamplerCompareOp compareOp = SamplerCompareOp::always;
    StencilOp failOp = StencilOp::keep;
    StencilOp depthFailOp = StencilOp::keep;
    StencilOp passOp = StencilOp::keep;
    std::uint32_t readMask = 0xffffffffu;
    std::uint32_t writeMask = 0xffffffffu;
};

struct DepthStencilStateDesc {
    SamplerCompareOp depthCompare = SamplerCompareOp::less_equal;
    bool stencilTest = false;
    StencilFaceStateDesc frontFaceStencil;
    StencilFaceStateDesc backFaceStencil;
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

struct DescriptorPolicyInfo {
    NativeDescriptorMappingModel mappingModel =
        NativeDescriptorMappingModel::direct_slots;
    NativeDescriptorAllocationModel allocationModel =
        NativeDescriptorAllocationModel::inline_direct;
    NativeDescriptorUpdateModel updateModel =
        NativeDescriptorUpdateModel::direct_write;
    NativeDescriptorBudgetModel budgetModel =
        NativeDescriptorBudgetModel::native_slot_spans;
    bool flattenedNativeBindings = false;
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
    DescriptorPolicyInfo descriptorPolicy;
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
    NativeDescriptorMappingModel descriptorMappingModel =
        NativeDescriptorMappingModel::direct_slots;
    NativeDescriptorAllocationModel descriptorAllocationModel =
        NativeDescriptorAllocationModel::inline_direct;
    NativeDescriptorUpdateModel descriptorUpdateModel =
        NativeDescriptorUpdateModel::direct_write;
    NativeDescriptorBudgetModel descriptorBudgetModel =
        NativeDescriptorBudgetModel::native_slot_spans;
    bool flattenedNativeBindings = false;
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

[[nodiscard]] constexpr bool texture_format_has_depth_aspect(
    TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::depth32_float:
    case TextureFormat::depth32_float_stencil8:
        return true;
    case TextureFormat::unknown:
    case TextureFormat::rgba8_unorm:
    case TextureFormat::bgra8_unorm:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr bool texture_format_has_stencil_aspect(
    TextureFormat format) noexcept {
    return format == TextureFormat::depth32_float_stencil8;
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

[[nodiscard]] constexpr bool descriptor_policy_flattens_native_bindings(
    const Capabilities& capabilities) noexcept {
    return capabilities.descriptorPolicy.flattenedNativeBindings;
}

[[nodiscard]] constexpr bool descriptor_policy_preserves_group_bindings(
    const Capabilities& capabilities) noexcept {
    return !descriptor_policy_flattens_native_bindings(capabilities);
}

[[nodiscard]] constexpr bool descriptor_policy_supports_direct_updates(
    const Capabilities& capabilities) noexcept {
    return capabilities.descriptorPolicy.updateModel ==
           NativeDescriptorUpdateModel::direct_write;
}

[[nodiscard]] constexpr bool descriptor_policy_updates_via_allocation_copies(
    const Capabilities& capabilities) noexcept {
    return capabilities.descriptorPolicy.updateModel ==
           NativeDescriptorUpdateModel::copy_into_allocation;
}

[[nodiscard]] constexpr bool descriptor_policy_rebuilds_allocations_for_updates(
    const Capabilities& capabilities) noexcept {
    return capabilities.descriptorPolicy.updateModel ==
           NativeDescriptorUpdateModel::rebuild_allocation;
}

[[nodiscard]] constexpr bool descriptor_policy_uses_native_slot_budgets(
    const Capabilities& capabilities) noexcept {
    return capabilities.descriptorPolicy.budgetModel ==
           NativeDescriptorBudgetModel::native_slot_spans;
}

[[nodiscard]] constexpr bool descriptor_policy_uses_descriptor_count_budgets(
    const Capabilities& capabilities) noexcept {
    return capabilities.descriptorPolicy.budgetModel ==
           NativeDescriptorBudgetModel::descriptor_count;
}

[[nodiscard]] constexpr bool descriptor_policy_uses_bind_group_budgets(
    const Capabilities& capabilities) noexcept {
    return capabilities.descriptorPolicy.budgetModel ==
           NativeDescriptorBudgetModel::bind_group_count;
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
    if (texture_format_has_depth_aspect(format)) {
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
    std::optional<std::uint32_t> nativeSlot;
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

enum class BindGroupAllocationPolicy {
    persistent,
    transient_frame,
};

enum class BindGroupReuseHint {
    stable,
    update_in_place,
    rebuild,
};

struct BindGroupLayoutDesc {
    std::string debugName;
    std::uint64_t cacheKey = 0;
    std::vector<BindingLayoutDesc> bindings;
};

struct BindGroupDesc {
    std::string debugName;
    std::uint64_t cacheKey = 0;
    IBindGroupLayout* layout = nullptr;
    BindGroupAllocationPolicy allocationPolicy =
        BindGroupAllocationPolicy::persistent;
    BindGroupReuseHint reuseHint = BindGroupReuseHint::stable;
    std::uint32_t allocationFrameIndex = 0;
    std::vector<BindGroupEntry> entries;
};

struct NativeDescriptorSpan {
    std::uint32_t firstSlot = 0;
    std::uint32_t slotCount = 0;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return slotCount == 0;
    }
};

struct BindGroupDescriptorFootprint {
    std::uint32_t bindingCount = 0;
    std::uint32_t descriptorCount = 0;
    std::uint32_t dynamicOffsetCount = 0;
    std::uint32_t bufferDescriptorCount = 0;
    std::uint32_t textureDescriptorCount = 0;
    std::uint32_t samplerDescriptorCount = 0;
    NativeDescriptorSpan bufferSlots;
    NativeDescriptorSpan textureSlots;
    NativeDescriptorSpan samplerSlots;
};

struct BindGroupDescriptorBudget {
    NativeDescriptorBudgetModel model =
        NativeDescriptorBudgetModel::native_slot_spans;
    std::uint32_t totalUnits = 0;
    std::uint32_t bufferUnits = 0;
    std::uint32_t textureUnits = 0;
    std::uint32_t samplerUnits = 0;
};

[[nodiscard]] constexpr std::uint32_t saturating_multiply_u32(
    std::uint32_t lhs,
    std::uint32_t rhs) noexcept {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }

    constexpr auto maxValue = std::numeric_limits<std::uint32_t>::max();
    return lhs > maxValue / rhs ? maxValue : lhs * rhs;
}

[[nodiscard]] constexpr std::uint32_t saturating_add_u32(
    std::uint32_t lhs,
    std::uint32_t rhs) noexcept {
    constexpr auto maxValue = std::numeric_limits<std::uint32_t>::max();
    return lhs > maxValue - rhs ? maxValue : lhs + rhs;
}

[[nodiscard]] constexpr bool bind_group_descriptor_budget_empty(
    const BindGroupDescriptorBudget& budget) noexcept {
    return budget.totalUnits == 0 && budget.bufferUnits == 0 &&
           budget.textureUnits == 0 && budget.samplerUnits == 0;
}

constexpr void include_bind_group_descriptor_budget(
    BindGroupDescriptorBudget&       total,
    const BindGroupDescriptorBudget& budget) noexcept {
    if (bind_group_descriptor_budget_empty(total)) {
        total.model = budget.model;
    }
    total.totalUnits = saturating_add_u32(total.totalUnits, budget.totalUnits);
    total.bufferUnits = saturating_add_u32(total.bufferUnits, budget.bufferUnits);
    total.textureUnits = saturating_add_u32(total.textureUnits, budget.textureUnits);
    total.samplerUnits = saturating_add_u32(total.samplerUnits, budget.samplerUnits);
}

constexpr void include_bind_group_descriptor_budget_peak(
    BindGroupDescriptorBudget&       peak,
    const BindGroupDescriptorBudget& budget) noexcept {
    if (bind_group_descriptor_budget_empty(peak)) {
        peak.model = budget.model;
    }
    peak.totalUnits =
        peak.totalUnits > budget.totalUnits ? peak.totalUnits : budget.totalUnits;
    peak.bufferUnits =
        peak.bufferUnits > budget.bufferUnits ? peak.bufferUnits : budget.bufferUnits;
    peak.textureUnits = peak.textureUnits > budget.textureUnits
                            ? peak.textureUnits
                            : budget.textureUnits;
    peak.samplerUnits = peak.samplerUnits > budget.samplerUnits
                            ? peak.samplerUnits
                            : budget.samplerUnits;
}

[[nodiscard]] constexpr BindGroupDescriptorBudget scale_bind_group_descriptor_budget(
    const BindGroupDescriptorBudget& budget,
    std::uint32_t                    multiplier) noexcept {
    BindGroupDescriptorBudget scaled = budget;
    scaled.totalUnits = saturating_multiply_u32(budget.totalUnits, multiplier);
    scaled.bufferUnits = saturating_multiply_u32(budget.bufferUnits, multiplier);
    scaled.textureUnits = saturating_multiply_u32(budget.textureUnits, multiplier);
    scaled.samplerUnits = saturating_multiply_u32(budget.samplerUnits, multiplier);
    return scaled;
}

enum class BindGroupCacheScope {
    none,
    persistent,
    per_frame,
};

enum class BindGroupDescriptorEvictionPolicy {
    manual,
    frame_retire,
    immediate,
};

struct BindGroupDescriptorStrategy {
    BindGroupCacheScope cacheScope = BindGroupCacheScope::none;
    bool rewriteDescriptors = false;
    bool rebuildAllocationOnUpdate = false;
    bool recycleAfterFrame = false;
    bool cacheKeyUsable = false;
    bool requiresFrameIndex = false;
    std::uint32_t frameSlotCount = 1;
    std::uint32_t recycleFrameLag = 0;
    BindGroupDescriptorBudget budget;
    BindGroupDescriptorEvictionPolicy evictionPolicy =
        BindGroupDescriptorEvictionPolicy::manual;
    NativeDescriptorMappingModel mappingModel =
        NativeDescriptorMappingModel::direct_slots;
    NativeDescriptorAllocationModel allocationModel =
        NativeDescriptorAllocationModel::inline_direct;
    NativeDescriptorUpdateModel updateModel =
        NativeDescriptorUpdateModel::direct_write;
    bool flattenedNativeBindings = false;
};

struct BindGroupDescriptorArenaPlan {
    std::uint32_t bindGroupCount = 0;
    std::uint32_t reservationMultiplier = 1;
    std::uint32_t cacheEntryCount = 0;
    std::uint32_t reservationEntryCount = 0;
    bool usesDescriptorCache = false;
    bool partitionsCachePerFrame = false;
    BindGroupDescriptorBudget budgetPerEntry;
    BindGroupDescriptorBudget cacheBudget;
    BindGroupDescriptorBudget reservationBudget;
};

struct BindGroupDescriptorArenaTotals {
    std::uint32_t planCount = 0;
    std::uint32_t bindGroupCount = 0;
    std::uint32_t cachedBindGroupCount = 0;
    std::uint32_t uncachedBindGroupCount = 0;
    std::uint32_t cacheEntryCount = 0;
    std::uint32_t reservationEntryCount = 0;
    std::uint32_t persistentCacheEntryCount = 0;
    std::uint32_t perFrameCacheEntryCount = 0;
    std::uint32_t uncachedReservationEntryCount = 0;
    std::uint32_t maxReservationMultiplier = 0;
    bool usesDescriptorCache = false;
    bool partitionsCachePerFrame = false;
    bool mixedBudgetModels = false;
    NativeDescriptorBudgetModel budgetModel =
        NativeDescriptorBudgetModel::native_slot_spans;
    BindGroupDescriptorBudget maxBudgetPerEntry;
    BindGroupDescriptorBudget cacheBudget;
    BindGroupDescriptorBudget reservationBudget;
    BindGroupDescriptorBudget persistentCacheBudget;
    BindGroupDescriptorBudget perFrameCacheBudget;
    BindGroupDescriptorBudget uncachedReservationBudget;
};

struct PipelineLayoutDescriptorBudget {
    std::uint32_t bindGroupCount = 0;
    BindGroupDescriptorBudget maxBudgetPerBindGroup;
    BindGroupDescriptorBudget totalBudget;
};

struct PipelineLayoutBindGroupArenaRequest {
    std::uint32_t groupIndex = 0;
    std::uint32_t bindGroupCount = 0;
    std::uint64_t cacheKey = 0;
    BindGroupAllocationPolicy allocationPolicy =
        BindGroupAllocationPolicy::persistent;
    BindGroupReuseHint reuseHint = BindGroupReuseHint::stable;
};

struct PipelineLayoutBindGroupArenaPlan {
    std::uint32_t groupIndex = 0;
    BindGroupLayoutDesc layout;
    BindGroupDescriptorStrategy strategy;
    BindGroupDescriptorArenaPlan arenaPlan;
};

struct PipelineLayoutDescriptorArenaSummary {
    std::uint32_t requestCount = 0;
    std::uint32_t plannedGroupCount = 0;
    std::uint32_t missingGroupCount = 0;
    bool complete = true;
    BindGroupDescriptorArenaTotals totals;
};

struct PipelineLayoutDescriptorArenaBatchRequest {
    const PipelineLayoutDesc* layout = nullptr;
    std::span<const PipelineLayoutBindGroupArenaRequest> requests;
};

struct SharedBindGroupDescriptorArenaFamily {
    BindGroupLayoutDesc           layout;
    BindGroupDescriptorStrategy   strategy;
    BindGroupDescriptorArenaPlan  arenaPlan;
    std::uint32_t                requestCount = 0;
};

struct SharedPipelineLayoutDescriptorArenaSummary {
    std::uint32_t layoutCount = 0;
    std::uint32_t requestCount = 0;
    std::uint32_t plannedGroupCount = 0;
    std::uint32_t missingLayoutCount = 0;
    std::uint32_t missingGroupCount = 0;
    std::uint32_t familyCount = 0;
    std::uint32_t mergedGroupCount = 0;
    std::uint32_t strategySplitGroupCount = 0;
    bool complete = true;
    BindGroupDescriptorArenaTotals totals;
    std::vector<SharedBindGroupDescriptorArenaFamily> families;
};

enum class BindGroupDescriptorArenaPoolClass {
    uncached_reservation,
    persistent_cache,
    per_frame_cache,
};

enum class BindGroupDescriptorLifetimeClass {
    retained_manual,
    frame_retired,
    immediate,
};

enum class BindGroupDescriptorLiveObjectScope {
    family,
    partition,
};

enum class BindGroupDescriptorReuseCohortKind {
    live_objects,
    capacity_only,
};

struct SharedBindGroupDescriptorArenaPartition {
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorStrategy strategy;
    std::uint32_t familyCount = 0;
    std::uint32_t requestCount = 0;
    std::uint32_t bindGroupCount = 0;
    std::uint32_t entryCount = 0;
    std::uint32_t reservationMultiplier = 1;
    std::uint32_t cacheKeyUsableFamilyCount = 0;
    std::uint32_t rewriteDescriptorFamilyCount = 0;
    std::uint32_t rebuildAllocationOnUpdateFamilyCount = 0;
    bool mixedCacheKeyUsability = false;
    bool mixedUpdateBehavior = false;
    bool mixedNativeUpdateModels = false;
    BindGroupDescriptorBudget totalBudget;
    std::vector<std::uint32_t> familyIndices;
};

struct SharedBindGroupDescriptorArenaFamilyResidency {
    std::uint32_t familyIndex = 0;
    std::uint32_t partitionIndex = 0;
    std::uint32_t requestCount = 0;
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    BindGroupDescriptorLiveObjectScope liveObjectScope =
        BindGroupDescriptorLiveObjectScope::family;
    bool sharesPartitionCapacity = true;
    bool cacheKeyUsable = false;
    bool rewriteDescriptors = false;
    bool rebuildAllocationOnUpdate = false;
    bool usesDescriptorCache = false;
    bool partitionsCachePerFrame = false;
    bool requiresFrameIndex = false;
    std::uint32_t frameSlotCount = 1;
    std::uint32_t recycleFrameLag = 0;
    std::uint32_t reservationMultiplier = 1;
    std::uint32_t bindGroupCount = 0;
    std::uint32_t entryCount = 0;
    BindGroupDescriptorEvictionPolicy evictionPolicy =
        BindGroupDescriptorEvictionPolicy::manual;
    NativeDescriptorUpdateModel updateModel =
        NativeDescriptorUpdateModel::direct_write;
    BindGroupDescriptorBudget budgetPerEntry;
    BindGroupDescriptorBudget totalBudget;
    bool partitionHasMixedCacheKeyUsability = false;
    bool partitionHasMixedUpdateBehavior = false;
    bool partitionHasMixedNativeUpdateModels = false;
};

struct SharedBindGroupDescriptorArenaReuseCohort {
    std::uint32_t partitionIndex = 0;
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorReuseCohortKind kind =
        BindGroupDescriptorReuseCohortKind::capacity_only;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    BindGroupDescriptorEvictionPolicy evictionPolicy =
        BindGroupDescriptorEvictionPolicy::manual;
    NativeDescriptorUpdateModel updateModel =
        NativeDescriptorUpdateModel::direct_write;
    bool rewriteDescriptors = false;
    bool rebuildAllocationOnUpdate = false;
    bool requiresFrameIndex = false;
    std::uint32_t frameSlotCount = 1;
    std::uint32_t recycleFrameLag = 0;
    std::uint32_t familyCount = 0;
    std::uint32_t requestCount = 0;
    std::uint32_t bindGroupCount = 0;
    std::uint32_t entryCount = 0;
    std::uint32_t cacheKeyUsableFamilyCount = 0;
    bool mixedCacheKeyUsability = false;
    BindGroupDescriptorBudget maxBudgetPerEntry;
    BindGroupDescriptorBudget totalBudget;
    std::vector<std::uint32_t> familyIndices;
};

struct SharedPipelineLayoutDescriptorArenaCohortSummary {
    std::uint32_t partitionCount = 0;
    std::uint32_t familyResidencyCount = 0;
    std::uint32_t cohortCount = 0;
    std::uint32_t liveObjectCohortCount = 0;
    std::uint32_t capacityOnlyCohortCount = 0;
    std::uint32_t mixedCacheKeyCohortCount = 0;
    bool complete = true;
    std::vector<SharedBindGroupDescriptorArenaReuseCohort> cohorts;
};

struct SharedPipelineLayoutDescriptorArenaPartitionSummary {
    std::uint32_t layoutCount = 0;
    std::uint32_t requestCount = 0;
    std::uint32_t plannedGroupCount = 0;
    std::uint32_t missingLayoutCount = 0;
    std::uint32_t missingGroupCount = 0;
    std::uint32_t familyCount = 0;
    std::uint32_t partitionCount = 0;
    std::uint32_t persistentCachePartitionCount = 0;
    std::uint32_t perFrameCachePartitionCount = 0;
    std::uint32_t uncachedReservationPartitionCount = 0;
    std::uint32_t mixedCacheKeyPartitionCount = 0;
    std::uint32_t mixedUpdatePartitionCount = 0;
    std::uint32_t familyResidencyCount = 0;
    std::uint32_t familyScopedLiveObjectCount = 0;
    std::uint32_t partitionScopedLiveObjectCount = 0;
    bool complete = true;
    std::vector<SharedBindGroupDescriptorArenaPartition> partitions;
    std::vector<SharedBindGroupDescriptorArenaFamilyResidency> familyResidencies;
};

struct SharedBindGroupDescriptorArenaMaterialization {
    std::uint32_t partitionIndex = 0;
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    bool usesDescriptorCache = false;
    bool partitionsCachePerFrame = false;
    bool requiresFrameIndex = false;
    std::uint32_t frameSlotCount = 1;
    std::uint32_t recycleFrameLag = 0;
    std::uint32_t reservationMultiplier = 1;
    std::uint32_t familyCount = 0;
    std::uint32_t cohortCount = 0;
    std::uint32_t requestCount = 0;
    std::uint32_t bindGroupCapacity = 0;
    std::uint32_t entryCapacity = 0;
    bool supportsPartitionWideLiveObjectReuse = false;
    bool mixedCacheKeyUsability = false;
    bool mixedUpdateBehavior = false;
    bool mixedNativeUpdateModels = false;
    BindGroupDescriptorEvictionPolicy evictionPolicy =
        BindGroupDescriptorEvictionPolicy::manual;
    BindGroupDescriptorBudget maxBudgetPerEntry;
    BindGroupDescriptorBudget totalBudget;
    std::vector<std::uint32_t> familyIndices;
    std::vector<std::uint32_t> cohortIndices;
};

struct SharedBindGroupDescriptorReuseMaterialization {
    std::uint32_t cohortIndex = 0;
    std::uint32_t partitionIndex = 0;
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorReuseCohortKind kind =
        BindGroupDescriptorReuseCohortKind::capacity_only;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    bool usesDescriptorCache = false;
    bool partitionsCachePerFrame = false;
    bool requiresFrameIndex = false;
    std::uint32_t frameSlotCount = 1;
    std::uint32_t recycleFrameLag = 0;
    std::uint32_t reservationMultiplier = 1;
    bool rewriteDescriptors = false;
    bool rebuildAllocationOnUpdate = false;
    bool supportsLiveObjectReuse = false;
    std::uint32_t familyCount = 0;
    std::uint32_t requestCount = 0;
    std::uint32_t bindGroupCapacity = 0;
    std::uint32_t entryCapacity = 0;
    std::uint32_t cacheKeyUsableFamilyCount = 0;
    bool mixedCacheKeyUsability = false;
    BindGroupDescriptorEvictionPolicy evictionPolicy =
        BindGroupDescriptorEvictionPolicy::manual;
    NativeDescriptorUpdateModel updateModel =
        NativeDescriptorUpdateModel::direct_write;
    BindGroupDescriptorBudget maxBudgetPerEntry;
    BindGroupDescriptorBudget totalBudget;
    std::vector<std::uint32_t> familyIndices;
};

struct SharedPipelineLayoutDescriptorArenaMaterializationSummary {
    std::uint32_t partitionCount = 0;
    std::uint32_t cohortCount = 0;
    std::uint32_t arenaCount = 0;
    std::uint32_t reuseMaterializationCount = 0;
    std::uint32_t liveObjectReuseMaterializationCount = 0;
    std::uint32_t capacityOnlyReuseMaterializationCount = 0;
    bool complete = true;
    std::vector<SharedBindGroupDescriptorArenaMaterialization> arenas;
    std::vector<SharedBindGroupDescriptorReuseMaterialization> reuseMaterializations;
};

struct SharedPipelineLayoutDescriptorArenaPlan {
    SharedPipelineLayoutDescriptorArenaSummary families;
    SharedPipelineLayoutDescriptorArenaPartitionSummary partitions;
    SharedPipelineLayoutDescriptorArenaCohortSummary cohorts;
    SharedPipelineLayoutDescriptorArenaMaterializationSummary materialization;
};

struct BindGroupDescriptorArenaReservationRequest {
    std::uint32_t bindGroupCount = 0;
    std::uint32_t entryCount = 0;
    std::uint32_t frameIndex = 0;
    bool liveObjectReuse = false;
};

struct BindGroupDescriptorArenaReservation {
    std::uint64_t id = 0;
    std::uint32_t partitionIndex = 0;
    std::uint32_t bindGroupCount = 0;
    std::uint32_t entryCount = 0;
    std::uint32_t frameIndex = 0;
    bool liveObjectReuse = false;
};

struct BindGroupDescriptorArenaSlotUsage {
    std::uint32_t slotIndex = 0;
    std::uint32_t reservationCount = 0;
    std::uint32_t usedBindGroupCount = 0;
    std::uint32_t usedEntryCount = 0;
    std::uint32_t availableBindGroupCount = 0;
    std::uint32_t availableEntryCount = 0;
};

struct BindGroupDescriptorArenaUsage {
    std::uint32_t reservationCount = 0;
    std::uint32_t usedBindGroupCount = 0;
    std::uint32_t usedEntryCount = 0;
    std::uint32_t availableBindGroupCount = 0;
    std::uint32_t availableEntryCount = 0;
    std::vector<BindGroupDescriptorArenaSlotUsage> slots;
    std::vector<BindGroupDescriptorArenaReservation> reservations;
};

struct BindGroupDescriptorArenaSlotRelease {
    std::uint32_t slotIndex = 0;
    std::uint32_t releasedReservationCount = 0;
    std::uint32_t releasedBindGroupCount = 0;
    std::uint32_t releasedEntryCount = 0;
};

struct BindGroupDescriptorReuseMaterializerSlotState {
    std::uint32_t slotIndex = 0;
    std::uint32_t activeReservationCount = 0;
    std::uint32_t activeBindGroupCount = 0;
    std::uint32_t activeEntryCount = 0;
};

struct BindGroupDescriptorReuseMaterializerState {
    std::uint32_t issuedRequestCount = 0;
    std::uint32_t issuedBindGroupCount = 0;
    std::uint32_t issuedEntryCount = 0;
    std::uint32_t activeReservationCount = 0;
    std::uint32_t activeBindGroupCount = 0;
    std::uint32_t activeEntryCount = 0;
    std::uint32_t liveObjectReservationCount = 0;
    std::uint32_t capacityOnlyReservationCount = 0;
    std::uint32_t nextFrameIndex = 0;
    std::optional<std::uint32_t> lastFrameIndex;
    std::vector<BindGroupDescriptorReuseMaterializerSlotState> slots;
    std::vector<BindGroupDescriptorArenaReservation> trackedReservations;
};

struct BindGroupDescriptorRuntimeCoordinatorState {
    bool compatible = false;
    bool drifted = false;
    bool underlyingReservationsConsistent = false;
    std::uint32_t trackedReservationCount = 0;
    std::uint32_t trackedBindGroupCount = 0;
    std::uint32_t trackedEntryCount = 0;
    BindGroupDescriptorArenaUsage arenaUsage;
    BindGroupDescriptorReuseMaterializerState reuseState;
    std::vector<BindGroupDescriptorArenaReservation> trackedReservations;
};

[[nodiscard]] constexpr bool bind_group_descriptor_reservations_equal(
    const BindGroupDescriptorArenaReservation& lhs,
    const BindGroupDescriptorArenaReservation& rhs) noexcept {
    return lhs.id == rhs.id && lhs.partitionIndex == rhs.partitionIndex &&
           lhs.bindGroupCount == rhs.bindGroupCount &&
           lhs.entryCount == rhs.entryCount &&
           lhs.frameIndex == rhs.frameIndex &&
           lhs.liveObjectReuse == rhs.liveObjectReuse;
}

[[nodiscard]] inline bool bind_group_descriptor_reservation_lists_equal(
    const std::vector<BindGroupDescriptorArenaReservation>& lhs,
    const std::vector<BindGroupDescriptorArenaReservation>& rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (const auto& leftReservation : lhs) {
        bool found = false;
        for (const auto& rightReservation : rhs) {
            if (bind_group_descriptor_reservations_equal(leftReservation,
                                                         rightReservation)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool binding_resource_type_is_buffer(
    BindingResourceType type) noexcept {
    return type == BindingResourceType::uniform_buffer ||
           type == BindingResourceType::storage_buffer;
}

[[nodiscard]] constexpr bool binding_resource_type_is_texture(
    BindingResourceType type) noexcept {
    return type == BindingResourceType::sampled_texture ||
           type == BindingResourceType::storage_texture;
}

[[nodiscard]] constexpr bool binding_resource_type_is_sampler(
    BindingResourceType type) noexcept {
    return type == BindingResourceType::sampler;
}

constexpr void include_native_descriptor_slots(
    NativeDescriptorSpan& span,
    std::uint32_t firstSlot,
    std::uint32_t slotCount) noexcept {
    if (slotCount == 0) {
        return;
    }
    if (span.slotCount == 0) {
        span.firstSlot = firstSlot;
        span.slotCount = slotCount;
        return;
    }

    const auto spanEnd = span.firstSlot + span.slotCount;
    const auto slotEnd = firstSlot + slotCount;
    const auto minSlot = span.firstSlot < firstSlot ? span.firstSlot : firstSlot;
    const auto maxEnd = spanEnd > slotEnd ? spanEnd : slotEnd;
    span.firstSlot = minSlot;
    span.slotCount = maxEnd - minSlot;
}

[[nodiscard]] inline BindGroupDescriptorFootprint bind_group_descriptor_footprint(
    const BindGroupLayoutDesc& desc) noexcept {
    BindGroupDescriptorFootprint footprint;
    footprint.bindingCount = static_cast<std::uint32_t>(desc.bindings.size());

    for (const auto& binding : desc.bindings) {
        footprint.descriptorCount += binding.arrayCount;
        if (binding.dynamicOffset) {
            footprint.dynamicOffsetCount += binding.arrayCount;
        }

        const auto firstSlot = binding.nativeSlot.value_or(binding.bindingIndex);
        if (binding_resource_type_is_buffer(binding.type)) {
            footprint.bufferDescriptorCount += binding.arrayCount;
            include_native_descriptor_slots(
                footprint.bufferSlots, firstSlot, binding.arrayCount);
        } else if (binding_resource_type_is_texture(binding.type)) {
            footprint.textureDescriptorCount += binding.arrayCount;
            include_native_descriptor_slots(
                footprint.textureSlots, firstSlot, binding.arrayCount);
        } else if (binding_resource_type_is_sampler(binding.type)) {
            footprint.samplerDescriptorCount += binding.arrayCount;
            include_native_descriptor_slots(
                footprint.samplerSlots, firstSlot, binding.arrayCount);
        }
    }

    return footprint;
}

[[nodiscard]] constexpr BindGroupDescriptorBudget bind_group_descriptor_budget(
    const BindGroupDescriptorFootprint& footprint,
    const Capabilities&                capabilities) noexcept {
    BindGroupDescriptorBudget budget;
    budget.model = capabilities.descriptorPolicy.budgetModel;

    switch (budget.model) {
    case NativeDescriptorBudgetModel::native_slot_spans:
        budget.bufferUnits = footprint.bufferSlots.slotCount;
        budget.textureUnits = footprint.textureSlots.slotCount;
        budget.samplerUnits = footprint.samplerSlots.slotCount;
        budget.totalUnits =
            budget.bufferUnits + budget.textureUnits + budget.samplerUnits;
        break;
    case NativeDescriptorBudgetModel::descriptor_count:
        budget.bufferUnits = footprint.bufferDescriptorCount;
        budget.textureUnits = footprint.textureDescriptorCount;
        budget.samplerUnits = footprint.samplerDescriptorCount;
        budget.totalUnits = footprint.descriptorCount;
        break;
    case NativeDescriptorBudgetModel::bind_group_count:
        budget.totalUnits =
            footprint.bindingCount == 0 && footprint.descriptorCount == 0 ? 0u : 1u;
        break;
    }

    return budget;
}

[[nodiscard]] inline BindGroupDescriptorBudget bind_group_descriptor_budget(
    const BindGroupLayoutDesc& desc,
    const Capabilities&       capabilities) noexcept {
    return bind_group_descriptor_budget(
        bind_group_descriptor_footprint(desc), capabilities);
}

[[nodiscard]] inline BindGroupDescriptorBudget bind_group_descriptor_budget(
    const BindGroupDesc& desc,
    const Capabilities& capabilities) noexcept;

[[nodiscard]] constexpr bool bind_group_prefers_descriptor_cache(
    const BindGroupDesc& desc) noexcept {
    return desc.reuseHint != BindGroupReuseHint::rebuild;
}

[[nodiscard]] constexpr bool bind_group_prefers_descriptor_rewrite(
    const BindGroupDesc& desc) noexcept {
    return desc.reuseHint == BindGroupReuseHint::update_in_place;
}

[[nodiscard]] constexpr bool bind_group_prefers_arena_recycling(
    const BindGroupDesc& desc) noexcept {
    return desc.allocationPolicy == BindGroupAllocationPolicy::transient_frame ||
           desc.reuseHint == BindGroupReuseHint::rebuild;
}

[[nodiscard]] constexpr std::uint32_t bind_group_descriptor_reservation_multiplier(
    const BindGroupDescriptorStrategy& strategy) noexcept {
    auto multiplier = strategy.frameSlotCount;
    if (strategy.recycleFrameLag > multiplier) {
        multiplier = strategy.recycleFrameLag;
    }
    return multiplier == 0 ? 1u : multiplier;
}

[[nodiscard]] constexpr BindGroupDescriptorStrategy bind_group_descriptor_strategy(
    BindGroupAllocationPolicy        allocationPolicy,
    BindGroupReuseHint               reuseHint,
    std::uint64_t                    cacheKey,
    const BindGroupDescriptorBudget& budget,
    const Capabilities&              capabilities) noexcept {
    BindGroupDescriptorStrategy strategy;
    strategy.mappingModel = capabilities.descriptorPolicy.mappingModel;
    strategy.allocationModel = capabilities.descriptorPolicy.allocationModel;
    strategy.updateModel = capabilities.descriptorPolicy.updateModel;
    strategy.flattenedNativeBindings =
        capabilities.descriptorPolicy.flattenedNativeBindings;
    strategy.rewriteDescriptors =
        reuseHint == BindGroupReuseHint::update_in_place &&
        strategy.updateModel != NativeDescriptorUpdateModel::rebuild_allocation;
    strategy.rebuildAllocationOnUpdate =
        reuseHint == BindGroupReuseHint::update_in_place &&
        strategy.updateModel == NativeDescriptorUpdateModel::rebuild_allocation;
    strategy.recycleAfterFrame =
        allocationPolicy == BindGroupAllocationPolicy::transient_frame;
    strategy.cacheKeyUsable =
        cacheKey != 0 && reuseHint != BindGroupReuseHint::rebuild;
    strategy.requiresFrameIndex =
        allocationPolicy == BindGroupAllocationPolicy::transient_frame;
    strategy.frameSlotCount =
        strategy.requiresFrameIndex ? capabilities.maxFramesInFlight : 1u;
    strategy.recycleFrameLag =
        strategy.requiresFrameIndex ? capabilities.maxFramesInFlight : 0u;
    strategy.budget = budget;
    strategy.evictionPolicy =
        reuseHint == BindGroupReuseHint::rebuild
            ? BindGroupDescriptorEvictionPolicy::immediate
        : allocationPolicy == BindGroupAllocationPolicy::transient_frame
            ? BindGroupDescriptorEvictionPolicy::frame_retire
            : BindGroupDescriptorEvictionPolicy::manual;

    if (reuseHint != BindGroupReuseHint::rebuild) {
        strategy.cacheScope =
            allocationPolicy == BindGroupAllocationPolicy::persistent
                ? BindGroupCacheScope::persistent
                : BindGroupCacheScope::per_frame;
    }

    return strategy;
}

[[nodiscard]] constexpr BindGroupDescriptorStrategy bind_group_descriptor_strategy(
    const BindGroupDesc& desc,
    const Capabilities&  capabilities) noexcept {
    return bind_group_descriptor_strategy(desc.allocationPolicy,
                                          desc.reuseHint,
                                          desc.cacheKey,
                                          bind_group_descriptor_budget(desc, capabilities),
                                          capabilities);
}

[[nodiscard]] constexpr BindGroupDescriptorArenaPlan bind_group_descriptor_arena_plan(
    const BindGroupDescriptorStrategy& strategy,
    std::uint32_t                      bindGroupCount) noexcept {
    BindGroupDescriptorArenaPlan plan;
    plan.bindGroupCount = bindGroupCount;
    plan.reservationMultiplier =
        bind_group_descriptor_reservation_multiplier(strategy);
    plan.usesDescriptorCache = strategy.cacheScope != BindGroupCacheScope::none;
    plan.partitionsCachePerFrame =
        strategy.cacheScope == BindGroupCacheScope::per_frame;
    plan.budgetPerEntry = strategy.budget;
    plan.cacheEntryCount = plan.usesDescriptorCache
                               ? saturating_multiply_u32(bindGroupCount,
                                                         plan.reservationMultiplier)
                               : 0u;
    plan.reservationEntryCount =
        saturating_multiply_u32(bindGroupCount, plan.reservationMultiplier);
    plan.cacheBudget =
        scale_bind_group_descriptor_budget(strategy.budget, plan.cacheEntryCount);
    plan.reservationBudget = scale_bind_group_descriptor_budget(
        strategy.budget, plan.reservationEntryCount);
    return plan;
}

[[nodiscard]] constexpr BindGroupDescriptorArenaPlan bind_group_descriptor_arena_plan(
    const BindGroupDesc& desc,
    const Capabilities&  capabilities,
    std::uint32_t        bindGroupCount) noexcept {
    return bind_group_descriptor_arena_plan(
        bind_group_descriptor_strategy(desc, capabilities), bindGroupCount);
}

constexpr void include_bind_group_descriptor_arena_plan(
    BindGroupDescriptorArenaTotals&     totals,
    const BindGroupDescriptorArenaPlan& plan) noexcept {
    if (totals.planCount == 0) {
        totals.budgetModel = plan.budgetPerEntry.model;
    } else if (totals.budgetModel != plan.budgetPerEntry.model) {
        totals.mixedBudgetModels = true;
    }

    ++totals.planCount;
    totals.bindGroupCount =
        saturating_add_u32(totals.bindGroupCount, plan.bindGroupCount);
    totals.cacheEntryCount =
        saturating_add_u32(totals.cacheEntryCount, plan.cacheEntryCount);
    totals.reservationEntryCount = saturating_add_u32(
        totals.reservationEntryCount, plan.reservationEntryCount);
    totals.maxReservationMultiplier =
        totals.maxReservationMultiplier > plan.reservationMultiplier
            ? totals.maxReservationMultiplier
            : plan.reservationMultiplier;
    include_bind_group_descriptor_budget_peak(totals.maxBudgetPerEntry,
                                              plan.budgetPerEntry);
    include_bind_group_descriptor_budget(totals.cacheBudget, plan.cacheBudget);
    include_bind_group_descriptor_budget(totals.reservationBudget,
                                         plan.reservationBudget);

    if (plan.usesDescriptorCache) {
        totals.usesDescriptorCache = true;
        totals.cachedBindGroupCount =
            saturating_add_u32(totals.cachedBindGroupCount, plan.bindGroupCount);
        if (plan.partitionsCachePerFrame) {
            totals.partitionsCachePerFrame = true;
            totals.perFrameCacheEntryCount = saturating_add_u32(
                totals.perFrameCacheEntryCount, plan.cacheEntryCount);
            include_bind_group_descriptor_budget(totals.perFrameCacheBudget,
                                                 plan.cacheBudget);
        } else {
            totals.persistentCacheEntryCount = saturating_add_u32(
                totals.persistentCacheEntryCount, plan.cacheEntryCount);
            include_bind_group_descriptor_budget(totals.persistentCacheBudget,
                                                 plan.cacheBudget);
        }
    } else {
        totals.uncachedBindGroupCount =
            saturating_add_u32(totals.uncachedBindGroupCount, plan.bindGroupCount);
        totals.uncachedReservationEntryCount = saturating_add_u32(
            totals.uncachedReservationEntryCount, plan.reservationEntryCount);
        include_bind_group_descriptor_budget(totals.uncachedReservationBudget,
                                             plan.reservationBudget);
    }
}

[[nodiscard]] constexpr BindGroupDescriptorArenaTotals
bind_group_descriptor_arena_totals(
    std::span<const BindGroupDescriptorArenaPlan> plans) noexcept {
    BindGroupDescriptorArenaTotals totals;
    for (const auto& plan : plans) {
        include_bind_group_descriptor_arena_plan(totals, plan);
    }
    return totals;
}

[[nodiscard]] constexpr bool binding_layout_compatible(
    const BindingLayoutDesc& expected,
    const BindingLayoutDesc& actual) noexcept {
    return expected.bindingIndex == actual.bindingIndex &&
           expected.type == actual.type &&
           expected.visibility == actual.visibility &&
           expected.arrayCount == actual.arrayCount &&
           expected.minBindingSize == actual.minBindingSize &&
           expected.dynamicIndexing == actual.dynamicIndexing &&
           expected.bindless == actual.bindless &&
           expected.dynamicOffset == actual.dynamicOffset &&
           expected.nativeSlot == actual.nativeSlot;
}

[[nodiscard]] inline bool bind_group_layout_compatible(
    const BindGroupLayoutDesc& expected,
    const BindGroupLayoutDesc& actual) noexcept {
    if (expected.bindings.size() != actual.bindings.size()) {
        return false;
    }

    for (const auto& expectedBinding : expected.bindings) {
        bool found = false;
        for (const auto& actualBinding : actual.bindings) {
            if (binding_layout_compatible(expectedBinding, actualBinding)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr bool bind_group_descriptor_strategy_partition_compatible(
    const BindGroupDescriptorStrategy& lhs,
    const BindGroupDescriptorStrategy& rhs) noexcept {
    return lhs.cacheScope == rhs.cacheScope &&
           lhs.rewriteDescriptors == rhs.rewriteDescriptors &&
           lhs.rebuildAllocationOnUpdate == rhs.rebuildAllocationOnUpdate &&
           lhs.recycleAfterFrame == rhs.recycleAfterFrame &&
           lhs.requiresFrameIndex == rhs.requiresFrameIndex &&
           lhs.frameSlotCount == rhs.frameSlotCount &&
           lhs.recycleFrameLag == rhs.recycleFrameLag &&
           lhs.budget.model == rhs.budget.model &&
           lhs.evictionPolicy == rhs.evictionPolicy &&
           lhs.mappingModel == rhs.mappingModel &&
           lhs.allocationModel == rhs.allocationModel &&
           lhs.updateModel == rhs.updateModel &&
           lhs.flattenedNativeBindings == rhs.flattenedNativeBindings;
}

[[nodiscard]] constexpr bool bind_group_descriptor_strategy_partition_reusable(
    const BindGroupDescriptorStrategy& lhs,
    const BindGroupDescriptorStrategy& rhs) noexcept {
    return lhs.cacheScope == rhs.cacheScope &&
           lhs.recycleAfterFrame == rhs.recycleAfterFrame &&
           lhs.requiresFrameIndex == rhs.requiresFrameIndex &&
           lhs.frameSlotCount == rhs.frameSlotCount &&
           lhs.recycleFrameLag == rhs.recycleFrameLag &&
           lhs.budget.model == rhs.budget.model &&
           lhs.evictionPolicy == rhs.evictionPolicy &&
           lhs.mappingModel == rhs.mappingModel &&
           lhs.allocationModel == rhs.allocationModel &&
           lhs.flattenedNativeBindings == rhs.flattenedNativeBindings;
}

[[nodiscard]] constexpr bool bind_group_descriptor_strategy_shareable(
    const BindGroupDescriptorStrategy& lhs,
    const BindGroupDescriptorStrategy& rhs) noexcept {
    return bind_group_descriptor_strategy_partition_compatible(lhs, rhs) &&
           lhs.budget.totalUnits == rhs.budget.totalUnits &&
           lhs.budget.bufferUnits == rhs.budget.bufferUnits &&
           lhs.budget.textureUnits == rhs.budget.textureUnits &&
           lhs.budget.samplerUnits == rhs.budget.samplerUnits;
}

[[nodiscard]] inline bool bind_group_descriptor_family_shareable(
    const BindGroupLayoutDesc&         lhsLayout,
    const BindGroupDescriptorStrategy& lhsStrategy,
    const BindGroupLayoutDesc&         rhsLayout,
    const BindGroupDescriptorStrategy& rhsStrategy) noexcept {
    return bind_group_layout_compatible(lhsLayout, rhsLayout) &&
           bind_group_layout_compatible(rhsLayout, lhsLayout) &&
           bind_group_descriptor_strategy_shareable(lhsStrategy, rhsStrategy);
}

[[nodiscard]] inline std::optional<BindGroupLayoutDesc>
pipeline_layout_bind_group_layout(const PipelineLayoutDesc& layout,
                                  std::uint32_t groupIndex) {
    BindGroupLayoutDesc bindGroupLayout;
    if (!layout.debugName.empty()) {
        bindGroupLayout.debugName =
            layout.debugName + ".group" + std::to_string(groupIndex);
    }

    for (const auto& binding : layout.bindings) {
        if (binding.groupIndex != groupIndex) {
            continue;
        }
        auto bindGroupBinding = binding;
        bindGroupBinding.groupIndex = 0;
        bindGroupLayout.bindings.push_back(std::move(bindGroupBinding));
    }

    if (bindGroupLayout.bindings.empty()) {
        return std::nullopt;
    }
    return bindGroupLayout;
}

[[nodiscard]] inline PipelineLayoutDescriptorBudget pipeline_layout_descriptor_budget(
    const PipelineLayoutDesc& layout,
    const Capabilities&       capabilities) {
    PipelineLayoutDescriptorBudget budget;
    for (std::size_t i = 0; i < layout.bindings.size(); ++i) {
        const auto groupIndex = layout.bindings[i].groupIndex;
        bool       seen       = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (layout.bindings[j].groupIndex == groupIndex) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }

        const auto groupLayout = pipeline_layout_bind_group_layout(layout, groupIndex);
        if (!groupLayout) {
            continue;
        }

        ++budget.bindGroupCount;
        const auto groupBudget =
            bind_group_descriptor_budget(*groupLayout, capabilities);
        include_bind_group_descriptor_budget_peak(budget.maxBudgetPerBindGroup,
                                                  groupBudget);
        include_bind_group_descriptor_budget(budget.totalBudget, groupBudget);
    }
    return budget;
}

[[nodiscard]] inline std::optional<PipelineLayoutBindGroupArenaPlan>
pipeline_layout_bind_group_arena_plan(
    const PipelineLayoutDesc&             layout,
    const Capabilities&                   capabilities,
    const PipelineLayoutBindGroupArenaRequest& request) {
    auto bindGroupLayout = pipeline_layout_bind_group_layout(layout, request.groupIndex);
    if (!bindGroupLayout) {
        return std::nullopt;
    }

    const auto strategy = bind_group_descriptor_strategy(
        request.allocationPolicy,
        request.reuseHint,
        request.cacheKey,
        bind_group_descriptor_budget(*bindGroupLayout, capabilities),
        capabilities);
    return PipelineLayoutBindGroupArenaPlan{
        .groupIndex = request.groupIndex,
        .layout = std::move(*bindGroupLayout),
        .strategy = strategy,
        .arenaPlan =
            bind_group_descriptor_arena_plan(strategy, request.bindGroupCount),
    };
}

[[nodiscard]] inline PipelineLayoutDescriptorArenaSummary
pipeline_layout_descriptor_arena_summary(
    const PipelineLayoutDesc&                      layout,
    const Capabilities&                            capabilities,
    std::span<const PipelineLayoutBindGroupArenaRequest> requests) {
    PipelineLayoutDescriptorArenaSummary summary;
    summary.requestCount = static_cast<std::uint32_t>(requests.size());
    for (const auto& request : requests) {
        const auto groupPlan =
            pipeline_layout_bind_group_arena_plan(layout, capabilities, request);
        if (!groupPlan) {
            summary.complete = false;
            summary.missingGroupCount =
                saturating_add_u32(summary.missingGroupCount, 1);
            continue;
        }

        summary.plannedGroupCount =
            saturating_add_u32(summary.plannedGroupCount, 1);
        include_bind_group_descriptor_arena_plan(summary.totals,
                                                 groupPlan->arenaPlan);
    }
    return summary;
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaSummary
pipeline_layout_shared_descriptor_arena_summary(
    const Capabilities&                                capabilities,
    std::span<const PipelineLayoutDescriptorArenaBatchRequest> batches) {
    SharedPipelineLayoutDescriptorArenaSummary summary;
    summary.layoutCount = static_cast<std::uint32_t>(batches.size());

    for (const auto& batch : batches) {
        summary.requestCount = saturating_add_u32(
            summary.requestCount,
            static_cast<std::uint32_t>(batch.requests.size()));
        if (batch.layout == nullptr) {
            summary.complete = false;
            summary.missingLayoutCount =
                saturating_add_u32(summary.missingLayoutCount, 1);
            continue;
        }

        for (const auto& request : batch.requests) {
            const auto groupPlan = pipeline_layout_bind_group_arena_plan(
                *batch.layout, capabilities, request);
            if (!groupPlan) {
                summary.complete = false;
                summary.missingGroupCount =
                    saturating_add_u32(summary.missingGroupCount, 1);
                continue;
            }

            summary.plannedGroupCount =
                saturating_add_u32(summary.plannedGroupCount, 1);

            bool mergedIntoExistingFamily = false;
            bool sawStrategySplit         = false;
            for (auto& family : summary.families) {
                const bool layoutCompatible =
                    bind_group_layout_compatible(groupPlan->layout, family.layout) &&
                    bind_group_layout_compatible(family.layout, groupPlan->layout);
                if (!layoutCompatible) {
                    continue;
                }

                if (!bind_group_descriptor_strategy_shareable(groupPlan->strategy,
                                                              family.strategy)) {
                    sawStrategySplit = true;
                    continue;
                }

                family.requestCount =
                    saturating_add_u32(family.requestCount, 1);
                family.strategy.cacheKeyUsable =
                    family.strategy.cacheKeyUsable &&
                    groupPlan->strategy.cacheKeyUsable;
                const auto mergedBindGroupCount = saturating_add_u32(
                    family.arenaPlan.bindGroupCount,
                    groupPlan->arenaPlan.bindGroupCount);
                family.arenaPlan = bind_group_descriptor_arena_plan(
                    family.strategy, mergedBindGroupCount);
                summary.mergedGroupCount =
                    saturating_add_u32(summary.mergedGroupCount, 1);
                mergedIntoExistingFamily = true;
                break;
            }

            if (mergedIntoExistingFamily) {
                continue;
            }
            if (sawStrategySplit) {
                summary.strategySplitGroupCount = saturating_add_u32(
                    summary.strategySplitGroupCount, 1);
            }

            summary.families.push_back({
                .layout = groupPlan->layout,
                .strategy = groupPlan->strategy,
                .arenaPlan = groupPlan->arenaPlan,
                .requestCount = 1,
            });
        }
    }

    summary.familyCount =
        static_cast<std::uint32_t>(summary.families.size());
    for (const auto& family : summary.families) {
        include_bind_group_descriptor_arena_plan(summary.totals,
                                                 family.arenaPlan);
    }
    return summary;
}

[[nodiscard]] constexpr BindGroupDescriptorArenaPoolClass
bind_group_descriptor_arena_pool_class(
    BindGroupCacheScope cacheScope) noexcept {
    switch (cacheScope) {
    case BindGroupCacheScope::persistent:
        return BindGroupDescriptorArenaPoolClass::persistent_cache;
    case BindGroupCacheScope::per_frame:
        return BindGroupDescriptorArenaPoolClass::per_frame_cache;
    case BindGroupCacheScope::none:
    default:
        return BindGroupDescriptorArenaPoolClass::uncached_reservation;
    }
}

[[nodiscard]] constexpr BindGroupDescriptorArenaPoolClass
bind_group_descriptor_arena_pool_class(
    const BindGroupDescriptorStrategy& strategy) noexcept {
    return bind_group_descriptor_arena_pool_class(strategy.cacheScope);
}

[[nodiscard]] constexpr BindGroupDescriptorLifetimeClass
bind_group_descriptor_lifetime_class(
    BindGroupDescriptorEvictionPolicy evictionPolicy) noexcept {
    switch (evictionPolicy) {
    case BindGroupDescriptorEvictionPolicy::frame_retire:
        return BindGroupDescriptorLifetimeClass::frame_retired;
    case BindGroupDescriptorEvictionPolicy::immediate:
        return BindGroupDescriptorLifetimeClass::immediate;
    case BindGroupDescriptorEvictionPolicy::manual:
    default:
        return BindGroupDescriptorLifetimeClass::retained_manual;
    }
}

[[nodiscard]] constexpr BindGroupDescriptorLifetimeClass
bind_group_descriptor_lifetime_class(
    const BindGroupDescriptorStrategy& strategy) noexcept {
    return bind_group_descriptor_lifetime_class(strategy.evictionPolicy);
}

[[nodiscard]] constexpr BindGroupDescriptorLiveObjectScope
bind_group_descriptor_live_object_scope(
    const SharedBindGroupDescriptorArenaPartition& partition) noexcept {
    return partition.familyCount == 1
               ? BindGroupDescriptorLiveObjectScope::partition
               : BindGroupDescriptorLiveObjectScope::family;
}

[[nodiscard]] constexpr BindGroupDescriptorReuseCohortKind
bind_group_descriptor_reuse_cohort_kind(
    BindGroupDescriptorLiveObjectScope liveObjectScope) noexcept {
    return liveObjectScope == BindGroupDescriptorLiveObjectScope::partition
               ? BindGroupDescriptorReuseCohortKind::live_objects
               : BindGroupDescriptorReuseCohortKind::capacity_only;
}

[[nodiscard]] constexpr BindGroupDescriptorReuseCohortKind
bind_group_descriptor_reuse_cohort_kind(
    const SharedBindGroupDescriptorArenaFamilyResidency& residency) noexcept {
    return bind_group_descriptor_reuse_cohort_kind(residency.liveObjectScope);
}

[[nodiscard]] constexpr std::uint32_t
bind_group_descriptor_arena_partition_entry_count(
    BindGroupDescriptorArenaPoolClass    poolClass,
    const BindGroupDescriptorArenaPlan& plan) noexcept {
    return poolClass == BindGroupDescriptorArenaPoolClass::uncached_reservation
               ? plan.reservationEntryCount
               : plan.cacheEntryCount;
}

[[nodiscard]] constexpr BindGroupDescriptorBudget
bind_group_descriptor_arena_partition_budget(
    BindGroupDescriptorArenaPoolClass    poolClass,
    const BindGroupDescriptorArenaPlan& plan) noexcept {
    return poolClass == BindGroupDescriptorArenaPoolClass::uncached_reservation
               ? plan.reservationBudget
               : plan.cacheBudget;
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaPartitionSummary
pipeline_layout_shared_descriptor_arena_partition_summary(
    const SharedPipelineLayoutDescriptorArenaSummary& familySummary) {
    SharedPipelineLayoutDescriptorArenaPartitionSummary partitionSummary;
    partitionSummary.layoutCount = familySummary.layoutCount;
    partitionSummary.requestCount = familySummary.requestCount;
    partitionSummary.plannedGroupCount = familySummary.plannedGroupCount;
    partitionSummary.missingLayoutCount = familySummary.missingLayoutCount;
    partitionSummary.missingGroupCount = familySummary.missingGroupCount;
    partitionSummary.familyCount = familySummary.familyCount;
    partitionSummary.complete = familySummary.complete;

    for (std::uint32_t familyIndex = 0;
         familyIndex < familySummary.families.size();
         ++familyIndex) {
        const auto& family = familySummary.families[familyIndex];
        const auto  poolClass =
            bind_group_descriptor_arena_pool_class(family.strategy);
        const auto entryCount = bind_group_descriptor_arena_partition_entry_count(
            poolClass, family.arenaPlan);
        const auto totalBudget = bind_group_descriptor_arena_partition_budget(
            poolClass, family.arenaPlan);

        bool mergedIntoExistingPartition = false;
        for (auto& partition : partitionSummary.partitions) {
            if (partition.poolClass != poolClass ||
                !bind_group_descriptor_strategy_partition_reusable(
                    partition.strategy, family.strategy)) {
                continue;
            }

            partition.familyCount =
                saturating_add_u32(partition.familyCount, 1);
            partition.requestCount = saturating_add_u32(
                partition.requestCount, family.requestCount);
            partition.bindGroupCount = saturating_add_u32(
                partition.bindGroupCount, family.arenaPlan.bindGroupCount);
            partition.entryCount =
                saturating_add_u32(partition.entryCount, entryCount);
            partition.reservationMultiplier =
                partition.reservationMultiplier >
                        family.arenaPlan.reservationMultiplier
                    ? partition.reservationMultiplier
                    : family.arenaPlan.reservationMultiplier;
            partition.cacheKeyUsableFamilyCount = saturating_add_u32(
                partition.cacheKeyUsableFamilyCount,
                family.strategy.cacheKeyUsable ? 1u : 0u);
            partition.rewriteDescriptorFamilyCount = saturating_add_u32(
                partition.rewriteDescriptorFamilyCount,
                family.strategy.rewriteDescriptors ? 1u : 0u);
            partition.rebuildAllocationOnUpdateFamilyCount =
                saturating_add_u32(
                    partition.rebuildAllocationOnUpdateFamilyCount,
                    family.strategy.rebuildAllocationOnUpdate ? 1u : 0u);
            partition.strategy.cacheKeyUsable =
                partition.cacheKeyUsableFamilyCount == partition.familyCount;
            partition.mixedCacheKeyUsability =
                partition.cacheKeyUsableFamilyCount != 0 &&
                partition.cacheKeyUsableFamilyCount != partition.familyCount;
            partition.strategy.rewriteDescriptors =
                partition.rewriteDescriptorFamilyCount != 0;
            partition.strategy.rebuildAllocationOnUpdate =
                partition.rebuildAllocationOnUpdateFamilyCount != 0;
            if (partition.strategy.updateModel != family.strategy.updateModel) {
                partition.mixedNativeUpdateModels = true;
            }
            const bool mixedRewriteBehavior =
                partition.rewriteDescriptorFamilyCount != 0 &&
                partition.rewriteDescriptorFamilyCount != partition.familyCount;
            const bool mixedRebuildBehavior =
                partition.rebuildAllocationOnUpdateFamilyCount != 0 &&
                partition.rebuildAllocationOnUpdateFamilyCount !=
                    partition.familyCount;
            const bool mixedRewriteAndRebuildFamilies =
                partition.rewriteDescriptorFamilyCount != 0 &&
                partition.rebuildAllocationOnUpdateFamilyCount != 0;
            partition.mixedUpdateBehavior =
                partition.mixedNativeUpdateModels || mixedRewriteBehavior ||
                mixedRebuildBehavior || mixedRewriteAndRebuildFamilies;
            include_bind_group_descriptor_budget_peak(partition.strategy.budget,
                                                      family.strategy.budget);
            include_bind_group_descriptor_budget(partition.totalBudget,
                                                 totalBudget);
            partition.familyIndices.push_back(familyIndex);
            mergedIntoExistingPartition = true;
            break;
        }

        if (!mergedIntoExistingPartition) {
            partitionSummary.partitions.push_back({
                .poolClass = poolClass,
                .strategy = family.strategy,
                .familyCount = 1,
                .requestCount = family.requestCount,
                .bindGroupCount = family.arenaPlan.bindGroupCount,
                .entryCount = entryCount,
                .reservationMultiplier = family.arenaPlan.reservationMultiplier,
                .cacheKeyUsableFamilyCount = family.strategy.cacheKeyUsable ? 1u : 0u,
                .rewriteDescriptorFamilyCount =
                    family.strategy.rewriteDescriptors ? 1u : 0u,
                .rebuildAllocationOnUpdateFamilyCount =
                    family.strategy.rebuildAllocationOnUpdate ? 1u : 0u,
                .totalBudget = totalBudget,
                .familyIndices = {familyIndex},
            });
        }
    }

    partitionSummary.partitionCount =
        static_cast<std::uint32_t>(partitionSummary.partitions.size());
    for (const auto& partition : partitionSummary.partitions) {
        switch (partition.poolClass) {
        case BindGroupDescriptorArenaPoolClass::persistent_cache:
            partitionSummary.persistentCachePartitionCount =
                saturating_add_u32(
                    partitionSummary.persistentCachePartitionCount, 1);
            break;
        case BindGroupDescriptorArenaPoolClass::per_frame_cache:
            partitionSummary.perFrameCachePartitionCount =
                saturating_add_u32(
                    partitionSummary.perFrameCachePartitionCount, 1);
            break;
        case BindGroupDescriptorArenaPoolClass::uncached_reservation:
        default:
            partitionSummary.uncachedReservationPartitionCount =
                saturating_add_u32(
                    partitionSummary.uncachedReservationPartitionCount, 1);
            break;
        }
        if (partition.mixedCacheKeyUsability) {
            partitionSummary.mixedCacheKeyPartitionCount = saturating_add_u32(
                partitionSummary.mixedCacheKeyPartitionCount, 1);
        }
        if (partition.mixedUpdateBehavior) {
            partitionSummary.mixedUpdatePartitionCount = saturating_add_u32(
                partitionSummary.mixedUpdatePartitionCount, 1);
        }
    }

    for (std::uint32_t partitionIndex = 0;
         partitionIndex < partitionSummary.partitions.size();
         ++partitionIndex) {
        const auto& partition = partitionSummary.partitions[partitionIndex];
        const auto liveObjectScope =
            bind_group_descriptor_live_object_scope(partition);
        for (const auto familyIndex : partition.familyIndices) {
            const auto& family = familySummary.families[familyIndex];
            const auto entryCount =
                bind_group_descriptor_arena_partition_entry_count(
                    partition.poolClass, family.arenaPlan);
            const auto totalBudget =
                bind_group_descriptor_arena_partition_budget(
                    partition.poolClass, family.arenaPlan);
            partitionSummary.familyResidencies.push_back({
                .familyIndex = familyIndex,
                .partitionIndex = partitionIndex,
                .requestCount = family.requestCount,
                .poolClass = partition.poolClass,
                .lifetimeClass =
                    bind_group_descriptor_lifetime_class(family.strategy),
                .liveObjectScope = liveObjectScope,
                .sharesPartitionCapacity = true,
                .cacheKeyUsable = family.strategy.cacheKeyUsable,
                .rewriteDescriptors = family.strategy.rewriteDescriptors,
                .rebuildAllocationOnUpdate =
                    family.strategy.rebuildAllocationOnUpdate,
                .usesDescriptorCache = family.arenaPlan.usesDescriptorCache,
                .partitionsCachePerFrame =
                    family.arenaPlan.partitionsCachePerFrame,
                .requiresFrameIndex = family.strategy.requiresFrameIndex,
                .frameSlotCount = family.strategy.frameSlotCount,
                .recycleFrameLag = family.strategy.recycleFrameLag,
                .reservationMultiplier =
                    family.arenaPlan.reservationMultiplier,
                .bindGroupCount = family.arenaPlan.bindGroupCount,
                .entryCount = entryCount,
                .evictionPolicy = family.strategy.evictionPolicy,
                .updateModel = family.strategy.updateModel,
                .budgetPerEntry = family.arenaPlan.budgetPerEntry,
                .totalBudget = totalBudget,
                .partitionHasMixedCacheKeyUsability =
                    partition.mixedCacheKeyUsability,
                .partitionHasMixedUpdateBehavior =
                    partition.mixedUpdateBehavior,
                .partitionHasMixedNativeUpdateModels =
                    partition.mixedNativeUpdateModels,
            });
            if (liveObjectScope ==
                BindGroupDescriptorLiveObjectScope::partition) {
                partitionSummary.partitionScopedLiveObjectCount =
                    saturating_add_u32(
                        partitionSummary.partitionScopedLiveObjectCount, 1);
            } else {
                partitionSummary.familyScopedLiveObjectCount =
                    saturating_add_u32(
                        partitionSummary.familyScopedLiveObjectCount, 1);
            }
        }
    }
    partitionSummary.familyResidencyCount = static_cast<std::uint32_t>(
        partitionSummary.familyResidencies.size());

    return partitionSummary;
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaCohortSummary
pipeline_layout_shared_descriptor_arena_cohort_summary(
    const SharedPipelineLayoutDescriptorArenaPartitionSummary& partitionSummary) {
    SharedPipelineLayoutDescriptorArenaCohortSummary cohortSummary;
    cohortSummary.partitionCount = partitionSummary.partitionCount;
    cohortSummary.familyResidencyCount = partitionSummary.familyResidencyCount;
    cohortSummary.complete = partitionSummary.complete;

    for (const auto& residency : partitionSummary.familyResidencies) {
        const auto kind = bind_group_descriptor_reuse_cohort_kind(residency);
        bool mergedIntoExistingCohort = false;
        for (auto& cohort : cohortSummary.cohorts) {
            if (cohort.partitionIndex != residency.partitionIndex ||
                cohort.poolClass != residency.poolClass || cohort.kind != kind ||
                cohort.lifetimeClass != residency.lifetimeClass ||
                cohort.evictionPolicy != residency.evictionPolicy ||
                cohort.updateModel != residency.updateModel ||
                cohort.rewriteDescriptors != residency.rewriteDescriptors ||
                cohort.rebuildAllocationOnUpdate !=
                    residency.rebuildAllocationOnUpdate ||
                cohort.requiresFrameIndex != residency.requiresFrameIndex ||
                cohort.frameSlotCount != residency.frameSlotCount ||
                cohort.recycleFrameLag != residency.recycleFrameLag) {
                continue;
            }

            cohort.familyCount = saturating_add_u32(cohort.familyCount, 1);
            cohort.requestCount =
                saturating_add_u32(cohort.requestCount, residency.requestCount);
            cohort.bindGroupCount =
                saturating_add_u32(cohort.bindGroupCount, residency.bindGroupCount);
            cohort.entryCount =
                saturating_add_u32(cohort.entryCount, residency.entryCount);
            cohort.cacheKeyUsableFamilyCount = saturating_add_u32(
                cohort.cacheKeyUsableFamilyCount,
                residency.cacheKeyUsable ? 1u : 0u);
            cohort.mixedCacheKeyUsability =
                cohort.cacheKeyUsableFamilyCount != 0 &&
                cohort.cacheKeyUsableFamilyCount != cohort.familyCount;
            include_bind_group_descriptor_budget_peak(cohort.maxBudgetPerEntry,
                                                      residency.budgetPerEntry);
            include_bind_group_descriptor_budget(cohort.totalBudget,
                                                 residency.totalBudget);
            cohort.familyIndices.push_back(residency.familyIndex);
            mergedIntoExistingCohort = true;
            break;
        }

        if (!mergedIntoExistingCohort) {
            cohortSummary.cohorts.push_back({
                .partitionIndex = residency.partitionIndex,
                .poolClass = residency.poolClass,
                .kind = kind,
                .lifetimeClass = residency.lifetimeClass,
                .evictionPolicy = residency.evictionPolicy,
                .updateModel = residency.updateModel,
                .rewriteDescriptors = residency.rewriteDescriptors,
                .rebuildAllocationOnUpdate =
                    residency.rebuildAllocationOnUpdate,
                .requiresFrameIndex = residency.requiresFrameIndex,
                .frameSlotCount = residency.frameSlotCount,
                .recycleFrameLag = residency.recycleFrameLag,
                .familyCount = 1,
                .requestCount = residency.requestCount,
                .bindGroupCount = residency.bindGroupCount,
                .entryCount = residency.entryCount,
                .cacheKeyUsableFamilyCount =
                    residency.cacheKeyUsable ? 1u : 0u,
                .maxBudgetPerEntry = residency.budgetPerEntry,
                .totalBudget = residency.totalBudget,
                .familyIndices = {residency.familyIndex},
            });
        }
    }

    cohortSummary.cohortCount =
        static_cast<std::uint32_t>(cohortSummary.cohorts.size());
    for (const auto& cohort : cohortSummary.cohorts) {
        if (cohort.kind == BindGroupDescriptorReuseCohortKind::live_objects) {
            cohortSummary.liveObjectCohortCount = saturating_add_u32(
                cohortSummary.liveObjectCohortCount, 1);
        } else {
            cohortSummary.capacityOnlyCohortCount = saturating_add_u32(
                cohortSummary.capacityOnlyCohortCount, 1);
        }
        if (cohort.mixedCacheKeyUsability) {
            cohortSummary.mixedCacheKeyCohortCount = saturating_add_u32(
                cohortSummary.mixedCacheKeyCohortCount, 1);
        }
    }

    return cohortSummary;
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaCohortSummary
pipeline_layout_shared_descriptor_arena_cohort_summary(
    const SharedPipelineLayoutDescriptorArenaSummary& familySummary) {
    return pipeline_layout_shared_descriptor_arena_cohort_summary(
        pipeline_layout_shared_descriptor_arena_partition_summary(familySummary));
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaPartitionSummary
pipeline_layout_shared_descriptor_arena_partition_summary(
    const Capabilities&                                capabilities,
    std::span<const PipelineLayoutDescriptorArenaBatchRequest> batches);

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaCohortSummary
pipeline_layout_shared_descriptor_arena_cohort_summary(
    const Capabilities&                                capabilities,
    std::span<const PipelineLayoutDescriptorArenaBatchRequest> batches) {
    return pipeline_layout_shared_descriptor_arena_cohort_summary(
        pipeline_layout_shared_descriptor_arena_partition_summary(capabilities,
                                                                  batches));
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaMaterializationSummary
pipeline_layout_shared_descriptor_arena_materialization_summary(
    const SharedPipelineLayoutDescriptorArenaPlan& plan) {
    SharedPipelineLayoutDescriptorArenaMaterializationSummary summary;
    summary.partitionCount = plan.partitions.partitionCount;
    summary.cohortCount = plan.cohorts.cohortCount;
    summary.complete =
        plan.families.complete && plan.partitions.complete && plan.cohorts.complete;

    for (std::uint32_t partitionIndex = 0;
         partitionIndex < plan.partitions.partitions.size();
         ++partitionIndex) {
        const auto& partition = plan.partitions.partitions[partitionIndex];
        SharedBindGroupDescriptorArenaMaterialization arena;
        arena.partitionIndex = partitionIndex;
        arena.poolClass = partition.poolClass;
        arena.lifetimeClass =
            bind_group_descriptor_lifetime_class(partition.strategy);
        arena.usesDescriptorCache =
            partition.poolClass !=
            BindGroupDescriptorArenaPoolClass::uncached_reservation;
        arena.partitionsCachePerFrame =
            partition.poolClass ==
            BindGroupDescriptorArenaPoolClass::per_frame_cache;
        arena.requiresFrameIndex = partition.strategy.requiresFrameIndex;
        arena.frameSlotCount = partition.strategy.frameSlotCount;
        arena.recycleFrameLag = partition.strategy.recycleFrameLag;
        arena.reservationMultiplier = partition.reservationMultiplier;
        arena.familyCount = partition.familyCount;
        arena.requestCount = partition.requestCount;
        arena.bindGroupCapacity = partition.bindGroupCount;
        arena.entryCapacity = partition.entryCount;
        arena.mixedCacheKeyUsability = partition.mixedCacheKeyUsability;
        arena.mixedUpdateBehavior = partition.mixedUpdateBehavior;
        arena.mixedNativeUpdateModels = partition.mixedNativeUpdateModels;
        arena.evictionPolicy = partition.strategy.evictionPolicy;
        arena.totalBudget = partition.totalBudget;
        arena.familyIndices = partition.familyIndices;

        for (std::uint32_t cohortIndex = 0;
             cohortIndex < plan.cohorts.cohorts.size();
             ++cohortIndex) {
            const auto& cohort = plan.cohorts.cohorts[cohortIndex];
            if (cohort.partitionIndex != partitionIndex) {
                continue;
            }
            arena.cohortIndices.push_back(cohortIndex);
            arena.cohortCount = saturating_add_u32(arena.cohortCount, 1);
            if (cohort.kind == BindGroupDescriptorReuseCohortKind::live_objects) {
                arena.supportsPartitionWideLiveObjectReuse =
                    cohort.familyCount == partition.familyCount;
            }
            include_bind_group_descriptor_budget_peak(arena.maxBudgetPerEntry,
                                                      cohort.maxBudgetPerEntry);
        }

        summary.arenas.push_back(std::move(arena));
    }

    for (std::uint32_t cohortIndex = 0; cohortIndex < plan.cohorts.cohorts.size();
         ++cohortIndex) {
        const auto& cohort = plan.cohorts.cohorts[cohortIndex];
        SharedBindGroupDescriptorReuseMaterialization reuse;
        reuse.cohortIndex = cohortIndex;
        reuse.partitionIndex = cohort.partitionIndex;
        reuse.poolClass = cohort.poolClass;
        reuse.kind = cohort.kind;
        reuse.lifetimeClass = cohort.lifetimeClass;
        reuse.usesDescriptorCache =
            cohort.poolClass !=
            BindGroupDescriptorArenaPoolClass::uncached_reservation;
        reuse.partitionsCachePerFrame =
            cohort.poolClass ==
            BindGroupDescriptorArenaPoolClass::per_frame_cache;
        reuse.requiresFrameIndex = cohort.requiresFrameIndex;
        reuse.frameSlotCount = cohort.frameSlotCount;
        reuse.recycleFrameLag = cohort.recycleFrameLag;
        reuse.reservationMultiplier =
            cohort.frameSlotCount > cohort.recycleFrameLag
                ? cohort.frameSlotCount
                : cohort.recycleFrameLag;
        if (reuse.reservationMultiplier == 0) {
            reuse.reservationMultiplier = 1;
        }
        reuse.rewriteDescriptors = cohort.rewriteDescriptors;
        reuse.rebuildAllocationOnUpdate = cohort.rebuildAllocationOnUpdate;
        reuse.supportsLiveObjectReuse =
            cohort.kind == BindGroupDescriptorReuseCohortKind::live_objects;
        reuse.familyCount = cohort.familyCount;
        reuse.requestCount = cohort.requestCount;
        reuse.bindGroupCapacity = cohort.bindGroupCount;
        reuse.entryCapacity = cohort.entryCount;
        reuse.cacheKeyUsableFamilyCount = cohort.cacheKeyUsableFamilyCount;
        reuse.mixedCacheKeyUsability = cohort.mixedCacheKeyUsability;
        reuse.evictionPolicy = cohort.evictionPolicy;
        reuse.updateModel = cohort.updateModel;
        reuse.maxBudgetPerEntry = cohort.maxBudgetPerEntry;
        reuse.totalBudget = cohort.totalBudget;
        reuse.familyIndices = cohort.familyIndices;
        summary.reuseMaterializations.push_back(std::move(reuse));
    }

    summary.arenaCount = static_cast<std::uint32_t>(summary.arenas.size());
    summary.reuseMaterializationCount = static_cast<std::uint32_t>(
        summary.reuseMaterializations.size());
    for (const auto& reuse : summary.reuseMaterializations) {
        if (reuse.supportsLiveObjectReuse) {
            summary.liveObjectReuseMaterializationCount =
                saturating_add_u32(summary.liveObjectReuseMaterializationCount, 1);
        } else {
            summary.capacityOnlyReuseMaterializationCount =
                saturating_add_u32(summary.capacityOnlyReuseMaterializationCount,
                                   1);
        }
    }

    return summary;
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaMaterializationSummary
pipeline_layout_shared_descriptor_arena_materialization_summary(
    const Capabilities&                                capabilities,
    std::span<const PipelineLayoutDescriptorArenaBatchRequest> batches) {
    SharedPipelineLayoutDescriptorArenaPlan plan;
    plan.families =
        pipeline_layout_shared_descriptor_arena_summary(capabilities, batches);
    plan.partitions =
        pipeline_layout_shared_descriptor_arena_partition_summary(plan.families);
    plan.cohorts =
        pipeline_layout_shared_descriptor_arena_cohort_summary(plan.partitions);
    return pipeline_layout_shared_descriptor_arena_materialization_summary(plan);
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaPartitionSummary
pipeline_layout_shared_descriptor_arena_partition_summary(
    const Capabilities&                                capabilities,
    std::span<const PipelineLayoutDescriptorArenaBatchRequest> batches) {
    return pipeline_layout_shared_descriptor_arena_partition_summary(
        pipeline_layout_shared_descriptor_arena_summary(capabilities, batches));
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaPlan
pipeline_layout_shared_descriptor_arena_plan(
    const SharedPipelineLayoutDescriptorArenaSummary& familySummary) {
    SharedPipelineLayoutDescriptorArenaPlan plan;
    plan.families = familySummary;
    plan.partitions =
        pipeline_layout_shared_descriptor_arena_partition_summary(plan.families);
    plan.cohorts =
        pipeline_layout_shared_descriptor_arena_cohort_summary(plan.partitions);
    plan.materialization =
        pipeline_layout_shared_descriptor_arena_materialization_summary(plan);
    return plan;
}

[[nodiscard]] inline SharedPipelineLayoutDescriptorArenaPlan
pipeline_layout_shared_descriptor_arena_plan(
    const Capabilities&                                capabilities,
    std::span<const PipelineLayoutDescriptorArenaBatchRequest> batches) {
    return pipeline_layout_shared_descriptor_arena_plan(
        pipeline_layout_shared_descriptor_arena_summary(capabilities, batches));
}

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
    ITexture* texture        = nullptr;          // nullptr = no depth/stencil attachment
    LoadOp    loadOp         = LoadOp::clear;
    StoreOp   storeOp        = StoreOp::dont_care;
    float     clearDepth     = 1.0f;
    LoadOp    stencilLoadOp  = LoadOp::dont_care;
    StoreOp   stencilStoreOp = StoreOp::dont_care;
    std::uint32_t clearStencil = 0;
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
    [[nodiscard]] virtual core::Result<void*> map() {
        return core::Status::failure(
            core::StatusCode::unsupported,
            "buffer mapping is not supported by this buffer");
    }
    [[nodiscard]] virtual core::Status unmap() {
        return core::Status::failure(
            core::StatusCode::unsupported,
            "buffer unmapping is not supported by this buffer");
    }
    [[nodiscard]] virtual bool mapped() const noexcept { return false; }
    [[nodiscard]] virtual void* mapped_data() noexcept { return nullptr; }
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
    [[nodiscard]] virtual std::uint64_t cache_key() const noexcept {
        return desc().cacheKey;
    }
    [[nodiscard]] virtual BindGroupDescriptorFootprint descriptor_footprint()
        const noexcept {
        return bind_group_descriptor_footprint(desc());
    }
};

[[nodiscard]] inline BindGroupDescriptorBudget bind_group_descriptor_budget(
    const BindGroupDesc& desc,
    const Capabilities& capabilities) noexcept {
    return desc.layout
               ? bind_group_descriptor_budget(desc.layout->desc(), capabilities)
               : BindGroupDescriptorBudget{
                     .model = capabilities.descriptorPolicy.budgetModel};
}

class IBindGroup {
public:
    virtual ~IBindGroup() = default;
    [[nodiscard]] virtual std::optional<BackendKind> backend_kind() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual const BindGroupDesc& desc() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t cache_key() const noexcept {
        return desc().cacheKey;
    }
    [[nodiscard]] virtual BindGroupAllocationPolicy allocation_policy()
        const noexcept {
        return desc().allocationPolicy;
    }
    [[nodiscard]] virtual std::uint32_t allocation_frame_index() const noexcept {
        return desc().allocationFrameIndex;
    }
    [[nodiscard]] virtual BindGroupReuseHint reuse_hint() const noexcept {
        return desc().reuseHint;
    }
    [[nodiscard]] virtual BindGroupDescriptorFootprint descriptor_footprint()
        const noexcept {
        return desc().layout ? bind_group_descriptor_footprint(desc().layout->desc())
                             : BindGroupDescriptorFootprint{};
    }
};

class IBindGroupDescriptorArena {
public:
    virtual ~IBindGroupDescriptorArena() = default;
    [[nodiscard]] virtual const SharedBindGroupDescriptorArenaMaterialization&
    desc() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t partition_index() const noexcept {
        return desc().partitionIndex;
    }
    [[nodiscard]] virtual BindGroupDescriptorArenaPoolClass pool_class()
        const noexcept {
        return desc().poolClass;
    }
    [[nodiscard]] virtual BindGroupDescriptorLifetimeClass lifetime_class()
        const noexcept {
        return desc().lifetimeClass;
    }
    [[nodiscard]] virtual std::uint32_t bind_group_capacity() const noexcept {
        return desc().bindGroupCapacity;
    }
    [[nodiscard]] virtual std::uint32_t entry_capacity() const noexcept {
        return desc().entryCapacity;
    }
    [[nodiscard]] virtual std::uint32_t slot_count() const noexcept {
        return desc().requiresFrameIndex && desc().frameSlotCount != 0
                   ? desc().frameSlotCount
                   : 1u;
    }
    [[nodiscard]] virtual std::uint32_t bind_group_capacity_per_slot()
        const noexcept {
        return desc().bindGroupCapacity;
    }
    [[nodiscard]] virtual std::uint32_t entry_capacity_per_slot() const noexcept {
        const auto slots = slot_count();
        return slots != 0 ? desc().entryCapacity / slots : desc().entryCapacity;
    }
    [[nodiscard]] virtual bool supports_live_object_reuse() const noexcept {
        return desc().supportsPartitionWideLiveObjectReuse;
    }
    [[nodiscard]] virtual BindGroupDescriptorArenaUsage usage() const noexcept = 0;
    [[nodiscard]] virtual bool can_reserve(
        const BindGroupDescriptorArenaReservationRequest& request) const noexcept = 0;
    [[nodiscard]] virtual core::Result<BindGroupDescriptorArenaReservation>
    reserve(const BindGroupDescriptorArenaReservationRequest& request) = 0;
    [[nodiscard]] virtual core::Status release(
        const BindGroupDescriptorArenaReservation& reservation) = 0;
    [[nodiscard]] virtual core::Result<BindGroupDescriptorArenaSlotRelease>
    retire_slot(std::uint32_t slotIndex) = 0;
    [[nodiscard]] virtual core::Status clear() = 0;
    [[nodiscard]] virtual bool empty() const noexcept {
        return usage().reservationCount == 0;
    }
};

class IBindGroupDescriptorReuseMaterializer {
public:
    virtual ~IBindGroupDescriptorReuseMaterializer() = default;
    [[nodiscard]] virtual const SharedBindGroupDescriptorReuseMaterialization&
    desc() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t cohort_index() const noexcept {
        return desc().cohortIndex;
    }
    [[nodiscard]] virtual std::uint32_t partition_index() const noexcept {
        return desc().partitionIndex;
    }
    [[nodiscard]] virtual BindGroupDescriptorArenaPoolClass pool_class()
        const noexcept {
        return desc().poolClass;
    }
    [[nodiscard]] virtual BindGroupDescriptorReuseCohortKind kind() const noexcept {
        return desc().kind;
    }
    [[nodiscard]] virtual std::uint32_t bind_group_capacity() const noexcept {
        return desc().bindGroupCapacity;
    }
    [[nodiscard]] virtual std::uint32_t entry_capacity() const noexcept {
        return desc().entryCapacity;
    }
    [[nodiscard]] virtual bool supports_live_object_reuse() const noexcept {
        return desc().supportsLiveObjectReuse;
    }
    [[nodiscard]] virtual bool compatible_with(
        const IBindGroupDescriptorArena& arena) const noexcept {
        return arena.partition_index() == partition_index() &&
               arena.pool_class() == pool_class() &&
               arena.bind_group_capacity() >= bind_group_capacity() &&
               arena.entry_capacity() >= entry_capacity() &&
               (!supports_live_object_reuse() ||
                arena.supports_live_object_reuse());
    }
    [[nodiscard]] virtual BindGroupDescriptorReuseMaterializerState state()
        const noexcept = 0;
    [[nodiscard]] virtual core::Result<BindGroupDescriptorArenaReservationRequest>
    make_reservation_request(std::uint32_t                    bindGroupCount,
                             std::optional<std::uint32_t>     frameIndex = std::nullopt,
                             bool liveObjectReuse = true) = 0;
    [[nodiscard]] virtual core::Status observe_reservation(
        const BindGroupDescriptorArenaReservation& reservation) = 0;
    [[nodiscard]] virtual core::Status release_reservation(
        const BindGroupDescriptorArenaReservation& reservation) = 0;
    [[nodiscard]] virtual core::Result<BindGroupDescriptorArenaSlotRelease>
    retire_slot(std::uint32_t slotIndex) = 0;
    [[nodiscard]] virtual core::Status clear() = 0;
    [[nodiscard]] virtual BindGroupDescriptorArenaReservationRequest
    reservation_request(std::uint32_t bindGroupCount,
                        std::uint32_t frameIndex = 0,
                        bool liveObjectReuse = true) const noexcept {
        return {
            .bindGroupCount = bindGroupCount,
            .entryCount = bindGroupCount,
            .frameIndex = frameIndex,
            .liveObjectReuse =
                liveObjectReuse && desc().supportsLiveObjectReuse,
        };
    }
};

class RetainedBindGroupDescriptorArena final : public IBindGroupDescriptorArena {
public:
    explicit RetainedBindGroupDescriptorArena(
        SharedBindGroupDescriptorArenaMaterialization desc)
        : desc_(std::move(desc)),
          slots_(desc_.requiresFrameIndex && desc_.frameSlotCount != 0
                     ? desc_.frameSlotCount
                     : 1u) {}

    [[nodiscard]] const SharedBindGroupDescriptorArenaMaterialization& desc()
        const noexcept override {
        return desc_;
    }

    [[nodiscard]] BindGroupDescriptorArenaUsage usage() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return usage_locked();
    }

    [[nodiscard]] bool can_reserve(
        const BindGroupDescriptorArenaReservationRequest& request) const noexcept
        override {
        std::lock_guard<std::mutex> lock(mutex_);
        return can_reserve_locked(request);
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaReservation> reserve(
        const BindGroupDescriptorArenaReservationRequest& request) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto slotIndex = resolve_slot_index(request);
        if (!slotIndex.has_value()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor arena reservation request is invalid");
        }
        if (!can_reserve_locked(request)) {
            return core::Status::failure(
                core::StatusCode::unavailable,
                "descriptor arena capacity is exhausted for the requested slot");
        }

        auto& slot = slots_[*slotIndex];
        slot.reservationCount = saturating_add_u32(slot.reservationCount, 1);
        slot.usedBindGroupCount = saturating_add_u32(slot.usedBindGroupCount,
                                                     request.bindGroupCount);
        slot.usedEntryCount =
            saturating_add_u32(slot.usedEntryCount, request.entryCount);

        BindGroupDescriptorArenaReservation reservation{
            .id = nextReservationId_++,
            .partitionIndex = desc_.partitionIndex,
            .bindGroupCount = request.bindGroupCount,
            .entryCount = request.entryCount,
            .frameIndex = request.frameIndex,
            .liveObjectReuse = request.liveObjectReuse,
        };
        reservations_.push_back(reservation);
        return reservation;
    }

    [[nodiscard]] core::Status release(
        const BindGroupDescriptorArenaReservation& reservation) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < reservations_.size(); ++i) {
            const auto& active = reservations_[i];
            if (active.id != reservation.id ||
                active.partitionIndex != reservation.partitionIndex) {
                continue;
            }

            const auto slotIndex = resolve_slot_index(active);
            if (!slotIndex.has_value()) {
                return core::Status::failure(
                    core::StatusCode::invalid_state,
                    "descriptor arena reservation state is corrupted");
            }

            auto& slot = slots_[*slotIndex];
            slot.reservationCount =
                slot.reservationCount > 0 ? slot.reservationCount - 1 : 0;
            slot.usedBindGroupCount =
                slot.usedBindGroupCount > active.bindGroupCount
                    ? slot.usedBindGroupCount - active.bindGroupCount
                    : 0;
            slot.usedEntryCount = slot.usedEntryCount > active.entryCount
                                      ? slot.usedEntryCount - active.entryCount
                                      : 0;
            reservations_.erase(reservations_.begin() +
                                static_cast<std::ptrdiff_t>(i));
            return core::Status::success();
        }

        return core::Status::failure(
            core::StatusCode::invalid_argument,
            "descriptor arena reservation does not belong to this arena");
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaSlotRelease> retire_slot(
        std::uint32_t slotIndex) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (slotIndex >= slots_.size()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor arena slot index is out of range");
        }
        if (!desc_.requiresFrameIndex && slotIndex != 0) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor arena exposes a single slot");
        }

        BindGroupDescriptorArenaSlotRelease released{
            .slotIndex = slotIndex,
            .releasedReservationCount = slots_[slotIndex].reservationCount,
            .releasedBindGroupCount = slots_[slotIndex].usedBindGroupCount,
            .releasedEntryCount = slots_[slotIndex].usedEntryCount,
        };

        if (released.releasedReservationCount == 0) {
            return released;
        }

        std::size_t writeIndex = 0;
        for (std::size_t readIndex = 0; readIndex < reservations_.size(); ++readIndex) {
            const auto reservationSlotIndex =
                resolve_slot_index(reservations_[readIndex]);
            if (reservationSlotIndex.has_value() &&
                *reservationSlotIndex == slotIndex) {
                continue;
            }
            if (writeIndex != readIndex) {
                reservations_[writeIndex] = std::move(reservations_[readIndex]);
            }
            ++writeIndex;
        }
        reservations_.resize(writeIndex);
        slots_[slotIndex] = {};
        return released;
    }

    [[nodiscard]] core::Status clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        reservations_.clear();
        for (auto& slot : slots_) {
            slot = {};
        }
        return core::Status::success();
    }

private:
    struct SlotState {
        std::uint32_t reservationCount = 0;
        std::uint32_t usedBindGroupCount = 0;
        std::uint32_t usedEntryCount = 0;
    };

    [[nodiscard]] std::optional<std::size_t> resolve_slot_index(
        const BindGroupDescriptorArenaReservationRequest& request) const noexcept {
        if (request.bindGroupCount == 0 || request.entryCount == 0 ||
            (request.liveObjectReuse &&
             !desc_.supportsPartitionWideLiveObjectReuse)) {
            return std::nullopt;
        }

        if (desc_.requiresFrameIndex) {
            return request.frameIndex < slots_.size()
                       ? std::optional<std::size_t>(
                             static_cast<std::size_t>(request.frameIndex))
                       : std::nullopt;
        }

        return request.frameIndex == 0 ? std::optional<std::size_t>(0u)
                                       : std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> resolve_slot_index(
        const BindGroupDescriptorArenaReservation& reservation) const noexcept {
        if (desc_.requiresFrameIndex) {
            return reservation.frameIndex < slots_.size()
                       ? std::optional<std::size_t>(
                             static_cast<std::size_t>(reservation.frameIndex))
                       : std::nullopt;
        }

        return reservation.frameIndex == 0 ? std::optional<std::size_t>(0u)
                                           : std::nullopt;
    }

    [[nodiscard]] bool can_reserve_locked(
        const BindGroupDescriptorArenaReservationRequest& request) const noexcept {
        const auto slotIndex = resolve_slot_index(request);
        if (!slotIndex.has_value()) {
            return false;
        }

        const auto& slot = slots_[*slotIndex];
        return request.bindGroupCount <=
                   bind_group_capacity_per_slot() - slot.usedBindGroupCount &&
               request.entryCount <=
                   entry_capacity_per_slot() - slot.usedEntryCount;
    }

    [[nodiscard]] BindGroupDescriptorArenaUsage usage_locked() const noexcept {
        BindGroupDescriptorArenaUsage usage;
        usage.slots.reserve(slots_.size());

        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const auto& slot = slots_[i];
            usage.reservationCount =
                saturating_add_u32(usage.reservationCount, slot.reservationCount);
            usage.usedBindGroupCount = saturating_add_u32(
                usage.usedBindGroupCount, slot.usedBindGroupCount);
            usage.usedEntryCount =
                saturating_add_u32(usage.usedEntryCount, slot.usedEntryCount);
            usage.slots.push_back({
                .slotIndex = static_cast<std::uint32_t>(i),
                .reservationCount = slot.reservationCount,
                .usedBindGroupCount = slot.usedBindGroupCount,
                .usedEntryCount = slot.usedEntryCount,
                .availableBindGroupCount =
                    bind_group_capacity_per_slot() - slot.usedBindGroupCount,
                .availableEntryCount =
                    entry_capacity_per_slot() - slot.usedEntryCount,
            });
        }
        usage.reservations = reservations_;

        usage.availableBindGroupCount = saturating_multiply_u32(
            bind_group_capacity_per_slot(), static_cast<std::uint32_t>(slots_.size()));
        usage.availableBindGroupCount =
            usage.availableBindGroupCount > usage.usedBindGroupCount
                ? usage.availableBindGroupCount - usage.usedBindGroupCount
                : 0;
        usage.availableEntryCount = desc_.entryCapacity > usage.usedEntryCount
                                        ? desc_.entryCapacity - usage.usedEntryCount
                                        : 0;
        return usage;
    }

    SharedBindGroupDescriptorArenaMaterialization desc_;
    mutable std::mutex                            mutex_;
    std::vector<SlotState>                        slots_;
    std::vector<BindGroupDescriptorArenaReservation> reservations_;
    std::uint64_t                                 nextReservationId_ = 1;
};

class RetainedBindGroupDescriptorReuseMaterializer final
    : public IBindGroupDescriptorReuseMaterializer {
public:
    explicit RetainedBindGroupDescriptorReuseMaterializer(
        SharedBindGroupDescriptorReuseMaterialization desc)
        : desc_(std::move(desc)),
          slots_(desc_.requiresFrameIndex && desc_.frameSlotCount != 0
                     ? desc_.frameSlotCount
                     : 1u) {}

    [[nodiscard]] const SharedBindGroupDescriptorReuseMaterialization& desc()
        const noexcept override {
        return desc_;
    }

    [[nodiscard]] BindGroupDescriptorReuseMaterializerState state()
        const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_locked();
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaReservationRequest>
    make_reservation_request(std::uint32_t                bindGroupCount,
                             std::optional<std::uint32_t> frameIndex,
                             bool liveObjectReuse) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto resolvedFrameIndex = resolve_frame_index(frameIndex);
        BindGroupDescriptorArenaReservationRequest request{
            .bindGroupCount = bindGroupCount,
            .entryCount = bindGroupCount,
            .frameIndex = resolvedFrameIndex,
            .liveObjectReuse =
                liveObjectReuse && desc_.supportsLiveObjectReuse,
        };
        if (!request_valid(request)) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor reuse materializer request is invalid");
        }

        issuedRequestCount_ = saturating_add_u32(issuedRequestCount_, 1);
        issuedBindGroupCount_ =
            saturating_add_u32(issuedBindGroupCount_, request.bindGroupCount);
        issuedEntryCount_ =
            saturating_add_u32(issuedEntryCount_, request.entryCount);
        lastFrameIndex_ = request.frameIndex;
        if (desc_.requiresFrameIndex && !slots_.empty()) {
            nextFrameIndex_ = static_cast<std::uint32_t>(
                (static_cast<std::size_t>(request.frameIndex) + 1u) % slots_.size());
        }
        return request;
    }

    [[nodiscard]] core::Status observe_reservation(
        const BindGroupDescriptorArenaReservation& reservation) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!reservation_valid(reservation)) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor reservation is incompatible with this reuse materializer");
        }
        if (contains_reservation(reservation.id)) {
            return core::Status::failure(
                core::StatusCode::invalid_state,
                "descriptor reservation is already tracked by this materializer");
        }

        const auto slotIndex = resolve_slot_index(reservation);
        if (!slotIndex.has_value()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor reservation frame index is invalid for this materializer");
        }

        auto& slot = slots_[*slotIndex];
        slot.activeReservationCount =
            saturating_add_u32(slot.activeReservationCount, 1);
        slot.activeBindGroupCount =
            saturating_add_u32(slot.activeBindGroupCount, reservation.bindGroupCount);
        slot.activeEntryCount =
            saturating_add_u32(slot.activeEntryCount, reservation.entryCount);
        trackedReservations_.push_back(reservation);
        return core::Status::success();
    }

    [[nodiscard]] core::Status release_reservation(
        const BindGroupDescriptorArenaReservation& reservation) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < trackedReservations_.size(); ++i) {
            const auto& active = trackedReservations_[i];
            if (active.id != reservation.id) {
                continue;
            }

            const auto slotIndex = resolve_slot_index(active);
            if (!slotIndex.has_value()) {
                return core::Status::failure(
                    core::StatusCode::invalid_state,
                    "descriptor reuse materializer state is corrupted");
            }

            auto& slot = slots_[*slotIndex];
            slot.activeReservationCount =
                slot.activeReservationCount > 0 ? slot.activeReservationCount - 1 : 0;
            slot.activeBindGroupCount =
                slot.activeBindGroupCount > active.bindGroupCount
                    ? slot.activeBindGroupCount - active.bindGroupCount
                    : 0;
            slot.activeEntryCount = slot.activeEntryCount > active.entryCount
                                        ? slot.activeEntryCount - active.entryCount
                                        : 0;
            trackedReservations_.erase(trackedReservations_.begin() +
                                       static_cast<std::ptrdiff_t>(i));
            return core::Status::success();
        }

        return core::Status::failure(
            core::StatusCode::invalid_argument,
            "descriptor reservation is not tracked by this materializer");
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaSlotRelease> retire_slot(
        std::uint32_t slotIndex) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (slotIndex >= slots_.size()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor reuse materializer slot index is out of range");
        }
        if (!desc_.requiresFrameIndex && slotIndex != 0) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor reuse materializer exposes a single slot");
        }

        BindGroupDescriptorArenaSlotRelease released{
            .slotIndex = slotIndex,
            .releasedReservationCount = slots_[slotIndex].activeReservationCount,
            .releasedBindGroupCount = slots_[slotIndex].activeBindGroupCount,
            .releasedEntryCount = slots_[slotIndex].activeEntryCount,
        };
        if (released.releasedReservationCount == 0) {
            return released;
        }

        std::size_t writeIndex = 0;
        for (std::size_t readIndex = 0; readIndex < trackedReservations_.size();
             ++readIndex) {
            const auto reservationSlotIndex =
                resolve_slot_index(trackedReservations_[readIndex]);
            if (reservationSlotIndex.has_value() &&
                *reservationSlotIndex == slotIndex) {
                continue;
            }
            if (writeIndex != readIndex) {
                trackedReservations_[writeIndex] =
                    std::move(trackedReservations_[readIndex]);
            }
            ++writeIndex;
        }
        trackedReservations_.resize(writeIndex);
        slots_[slotIndex] = {};
        return released;
    }

    [[nodiscard]] core::Status clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        trackedReservations_.clear();
        for (auto& slot : slots_) {
            slot = {};
        }
        issuedRequestCount_ = 0;
        issuedBindGroupCount_ = 0;
        issuedEntryCount_ = 0;
        nextFrameIndex_ = 0;
        lastFrameIndex_.reset();
        return core::Status::success();
    }

private:
    struct SlotState {
        std::uint32_t activeReservationCount = 0;
        std::uint32_t activeBindGroupCount = 0;
        std::uint32_t activeEntryCount = 0;
    };

    [[nodiscard]] std::uint32_t resolve_frame_index(
        std::optional<std::uint32_t> frameIndex) const noexcept {
        if (!desc_.requiresFrameIndex) {
            return 0;
        }
        return frameIndex.value_or(nextFrameIndex_);
    }

    [[nodiscard]] bool request_valid(
        const BindGroupDescriptorArenaReservationRequest& request) const noexcept {
        if (request.bindGroupCount == 0 || request.entryCount == 0 ||
            request.bindGroupCount > desc_.bindGroupCapacity ||
            request.entryCount > entry_capacity_per_slot() ||
            (request.liveObjectReuse && !desc_.supportsLiveObjectReuse)) {
            return false;
        }
        if (desc_.requiresFrameIndex) {
            return request.frameIndex < slots_.size();
        }
        return request.frameIndex == 0;
    }

    [[nodiscard]] std::optional<std::size_t> resolve_slot_index(
        const BindGroupDescriptorArenaReservation& reservation) const noexcept {
        if (desc_.requiresFrameIndex) {
            return reservation.frameIndex < slots_.size()
                       ? std::optional<std::size_t>(
                             static_cast<std::size_t>(reservation.frameIndex))
                       : std::nullopt;
        }
        return reservation.frameIndex == 0 ? std::optional<std::size_t>(0u)
                                           : std::nullopt;
    }

    [[nodiscard]] bool reservation_valid(
        const BindGroupDescriptorArenaReservation& reservation) const noexcept {
        return reservation.partitionIndex == desc_.partitionIndex &&
               reservation.bindGroupCount <= desc_.bindGroupCapacity &&
               reservation.entryCount <= entry_capacity_per_slot() &&
               (!reservation.liveObjectReuse || desc_.supportsLiveObjectReuse) &&
               resolve_slot_index(reservation).has_value();
    }

    [[nodiscard]] bool contains_reservation(std::uint64_t reservationId) const noexcept {
        for (const auto& reservation : trackedReservations_) {
            if (reservation.id == reservationId) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::uint32_t entry_capacity_per_slot() const noexcept {
        const auto slotCount =
            desc_.requiresFrameIndex && desc_.frameSlotCount != 0
                ? desc_.frameSlotCount
                : 1u;
        return slotCount != 0 ? desc_.entryCapacity / slotCount : desc_.entryCapacity;
    }

    [[nodiscard]] BindGroupDescriptorReuseMaterializerState state_locked()
        const noexcept {
        BindGroupDescriptorReuseMaterializerState state;
        state.issuedRequestCount = issuedRequestCount_;
        state.issuedBindGroupCount = issuedBindGroupCount_;
        state.issuedEntryCount = issuedEntryCount_;
        state.nextFrameIndex = nextFrameIndex_;
        state.lastFrameIndex = lastFrameIndex_;
        state.slots.reserve(slots_.size());

        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const auto& slot = slots_[i];
            state.activeReservationCount = saturating_add_u32(
                state.activeReservationCount, slot.activeReservationCount);
            state.activeBindGroupCount = saturating_add_u32(
                state.activeBindGroupCount, slot.activeBindGroupCount);
            state.activeEntryCount =
                saturating_add_u32(state.activeEntryCount, slot.activeEntryCount);
            state.slots.push_back({
                .slotIndex = static_cast<std::uint32_t>(i),
                .activeReservationCount = slot.activeReservationCount,
                .activeBindGroupCount = slot.activeBindGroupCount,
                .activeEntryCount = slot.activeEntryCount,
            });
        }

        for (const auto& reservation : trackedReservations_) {
            if (reservation.liveObjectReuse) {
                state.liveObjectReservationCount = saturating_add_u32(
                    state.liveObjectReservationCount, 1);
            } else {
                state.capacityOnlyReservationCount = saturating_add_u32(
                    state.capacityOnlyReservationCount, 1);
            }
        }
        state.trackedReservations = trackedReservations_;
        return state;
    }

    SharedBindGroupDescriptorReuseMaterialization desc_;
    mutable std::mutex                            mutex_;
    std::vector<SlotState>                        slots_;
    std::vector<BindGroupDescriptorArenaReservation> trackedReservations_;
    std::uint32_t                                 issuedRequestCount_ = 0;
    std::uint32_t                                 issuedBindGroupCount_ = 0;
    std::uint32_t                                 issuedEntryCount_ = 0;
    std::uint32_t                                 nextFrameIndex_ = 0;
    std::optional<std::uint32_t>                  lastFrameIndex_;
};

// Coordinates one descriptor arena and one reuse materializer as a single
// low-level reservation surface. Callers should reserve and release through
// this helper consistently so its tracked reservation view stays authoritative.
class BindGroupDescriptorRuntimeCoordinator {
public:
    BindGroupDescriptorRuntimeCoordinator(IBindGroupDescriptorArena& arena,
                                          IBindGroupDescriptorReuseMaterializer& reuse)
        : arena_(&arena), reuse_(&reuse) {}

    [[nodiscard]] IBindGroupDescriptorArena& arena() noexcept {
        return *arena_;
    }

    [[nodiscard]] const IBindGroupDescriptorArena& arena() const noexcept {
        return *arena_;
    }

    [[nodiscard]] IBindGroupDescriptorReuseMaterializer& reuse_materializer()
        noexcept {
        return *reuse_;
    }

    [[nodiscard]] const IBindGroupDescriptorReuseMaterializer&
    reuse_materializer() const noexcept {
        return *reuse_;
    }

    [[nodiscard]] bool compatible() const noexcept {
        return reuse_->compatible_with(*arena_);
    }

    [[nodiscard]] bool drifted() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return drifted_locked();
    }

    [[nodiscard]] BindGroupDescriptorRuntimeCoordinatorState state() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_locked();
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaReservationRequest>
    make_reservation_request(std::uint32_t                bindGroupCount,
                             std::optional<std::uint32_t> frameIndex = std::nullopt,
                             bool liveObjectReuse = true) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!compatible_locked()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor arena and reuse materializer are incompatible");
        }
        return request_locked(bindGroupCount, frameIndex, liveObjectReuse);
    }

    [[nodiscard]] bool can_reserve(
        std::uint32_t                     bindGroupCount,
        std::optional<std::uint32_t>      frameIndex = std::nullopt,
        bool liveObjectReuse = true) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!compatible_locked() || drifted_locked()) {
            return false;
        }
        auto request = request_locked(bindGroupCount, frameIndex, liveObjectReuse);
        return request.ok() && arena_->can_reserve(request.value());
    }

    [[nodiscard]] core::Status reconcile() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!compatible_locked()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor arena and reuse materializer are incompatible");
        }

        const auto arenaUsage = arena_->usage();
        const auto reuseState = reuse_->state();
        if (!bind_group_descriptor_reservation_lists_equal(arenaUsage.reservations,
                                                           reuseState.trackedReservations)) {
            return core::Status::failure(
                core::StatusCode::invalid_state,
                "descriptor arena and reuse materializer reservations disagree");
        }

        trackedReservations_ = arenaUsage.reservations;
        return core::Status::success();
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaReservation> reserve(
        std::uint32_t                     bindGroupCount,
        std::optional<std::uint32_t>      frameIndex = std::nullopt,
        bool liveObjectReuse = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!compatible_locked()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor arena and reuse materializer are incompatible");
        }
        if (drifted_locked()) {
            return core::Status::failure(
                core::StatusCode::invalid_state,
                "descriptor runtime coordinator tracking is stale; reconcile before "
                "reserving");
        }

        auto request = reuse_->make_reservation_request(bindGroupCount,
                                                        frameIndex,
                                                        liveObjectReuse);
        if (!request.ok()) {
            return request.status();
        }

        auto reservation = arena_->reserve(request.value());
        if (!reservation.ok()) {
            return reservation.status();
        }

        auto observeStatus = reuse_->observe_reservation(reservation.value());
        if (!observeStatus.ok()) {
            auto rollbackStatus = arena_->release(reservation.value());
            if (!rollbackStatus.ok()) {
                return core::Status::failure(
                    core::StatusCode::invalid_state,
                    "descriptor runtime reservation rollback failed after "
                    "materializer observation failed");
            }
            return observeStatus;
        }

        trackedReservations_.push_back(reservation.value());
        return reservation.value();
    }

    [[nodiscard]] core::Status release(
        const BindGroupDescriptorArenaReservation& reservation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (drifted_locked()) {
            return core::Status::failure(
                core::StatusCode::invalid_state,
                "descriptor runtime coordinator tracking is stale; reconcile before "
                "releasing");
        }
        auto trackedIndex = tracked_reservation_index_locked(reservation.id);
        if (!trackedIndex.has_value()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor runtime coordinator does not track this reservation");
        }

        const auto trackedReservation = trackedReservations_[*trackedIndex];
        auto reuseStatus = reuse_->release_reservation(trackedReservation);
        if (!reuseStatus.ok()) {
            return reuseStatus;
        }

        auto arenaStatus = arena_->release(trackedReservation);
        if (!arenaStatus.ok()) {
            auto rollbackStatus = reuse_->observe_reservation(trackedReservation);
            if (!rollbackStatus.ok()) {
                return core::Status::failure(
                    core::StatusCode::invalid_state,
                    "descriptor runtime release rollback failed after arena "
                    "release error");
            }
            return arenaStatus;
        }

        trackedReservations_.erase(trackedReservations_.begin() +
                                   static_cast<std::ptrdiff_t>(*trackedIndex));
        return core::Status::success();
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaSlotRelease> retire_slot(
        std::uint32_t slotIndex) {
        std::vector<BindGroupDescriptorArenaReservation> slotReservations;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (drifted_locked()) {
                return core::Status::failure(
                    core::StatusCode::invalid_state,
                    "descriptor runtime coordinator tracking is stale; reconcile "
                    "before retiring a slot");
            }
            if (!slot_index_valid_locked(slotIndex)) {
                return core::Status::failure(
                    core::StatusCode::invalid_argument,
                    "descriptor runtime coordinator slot index is invalid");
            }

            for (const auto& reservation : trackedReservations_) {
                if (reservation_slot_index_locked(reservation) == slotIndex) {
                    slotReservations.push_back(reservation);
                }
            }
        }

        BindGroupDescriptorArenaSlotRelease released{.slotIndex = slotIndex};
        for (const auto& reservation : slotReservations) {
            auto releaseStatus = release(reservation);
            if (!releaseStatus.ok()) {
                return releaseStatus;
            }
            released.releasedReservationCount =
                saturating_add_u32(released.releasedReservationCount, 1);
            released.releasedBindGroupCount =
                saturating_add_u32(released.releasedBindGroupCount,
                                   reservation.bindGroupCount);
            released.releasedEntryCount =
                saturating_add_u32(released.releasedEntryCount,
                                   reservation.entryCount);
        }
        return released;
    }

    [[nodiscard]] core::Status clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto arenaStatus = arena_->clear();
        if (!arenaStatus.ok()) {
            return arenaStatus;
        }
        auto reuseStatus = reuse_->clear();
        if (!reuseStatus.ok()) {
            return reuseStatus;
        }

        trackedReservations_.clear();
        return core::Status::success();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto arenaUsage = arena_->usage();
        const auto reuseState = reuse_->state();
        return trackedReservations_.empty() && arenaUsage.reservationCount == 0 &&
               reuseState.activeReservationCount == 0;
    }

private:
    [[nodiscard]] bool compatible_locked() const noexcept {
        return reuse_->compatible_with(*arena_);
    }

    [[nodiscard]] bool drifted_locked() const noexcept {
        const auto arenaUsage = arena_->usage();
        const auto reuseState = reuse_->state();
        return !bind_group_descriptor_reservation_lists_equal(arenaUsage.reservations,
                                                              reuseState.trackedReservations) ||
               !bind_group_descriptor_reservation_lists_equal(trackedReservations_,
                                                              arenaUsage.reservations) ||
               !bind_group_descriptor_reservation_lists_equal(
                   trackedReservations_, reuseState.trackedReservations);
    }

    [[nodiscard]] core::Result<BindGroupDescriptorArenaReservationRequest>
    request_locked(std::uint32_t                bindGroupCount,
                   std::optional<std::uint32_t> frameIndex,
                   bool liveObjectReuse) const {
        const auto reuseState = reuse_->state();
        const auto resolvedFrameIndex =
            reuse_->desc().requiresFrameIndex
                ? frameIndex.value_or(reuseState.nextFrameIndex)
                : 0u;
        auto request = reuse_->reservation_request(bindGroupCount,
                                                   resolvedFrameIndex,
                                                   liveObjectReuse);
        if (request.bindGroupCount == 0 || request.entryCount == 0 ||
            request.bindGroupCount > reuse_->bind_group_capacity() ||
            request.entryCount > reuse_->entry_capacity() ||
            (reuse_->desc().requiresFrameIndex &&
             request.frameIndex >= arena_->slot_count()) ||
            (!reuse_->desc().requiresFrameIndex && request.frameIndex != 0)) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "descriptor runtime reservation request is invalid");
        }
        return request;
    }

    [[nodiscard]] std::optional<std::size_t> tracked_reservation_index_locked(
        std::uint64_t reservationId) const noexcept {
        for (std::size_t i = 0; i < trackedReservations_.size(); ++i) {
            if (trackedReservations_[i].id == reservationId) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool slot_index_valid_locked(std::uint32_t slotIndex) const noexcept {
        if (slotIndex >= arena_->slot_count()) {
            return false;
        }
        return arena_->desc().requiresFrameIndex || slotIndex == 0;
    }

    [[nodiscard]] std::uint32_t reservation_slot_index_locked(
        const BindGroupDescriptorArenaReservation& reservation) const noexcept {
        return arena_->desc().requiresFrameIndex ? reservation.frameIndex : 0u;
    }

    [[nodiscard]] BindGroupDescriptorRuntimeCoordinatorState state_locked() const
        noexcept {
        BindGroupDescriptorRuntimeCoordinatorState state;
        state.compatible = compatible_locked();
        state.arenaUsage = arena_->usage();
        state.reuseState = reuse_->state();
        state.trackedReservations = trackedReservations_;
        state.underlyingReservationsConsistent =
            bind_group_descriptor_reservation_lists_equal(
                state.arenaUsage.reservations, state.reuseState.trackedReservations);
        state.drifted = !state.underlyingReservationsConsistent ||
                        !bind_group_descriptor_reservation_lists_equal(
                            trackedReservations_, state.arenaUsage.reservations) ||
                        !bind_group_descriptor_reservation_lists_equal(
                            trackedReservations_, state.reuseState.trackedReservations);
        state.trackedReservationCount =
            static_cast<std::uint32_t>(trackedReservations_.size());
        for (const auto& reservation : trackedReservations_) {
            state.trackedBindGroupCount = saturating_add_u32(
                state.trackedBindGroupCount, reservation.bindGroupCount);
            state.trackedEntryCount =
                saturating_add_u32(state.trackedEntryCount, reservation.entryCount);
        }
        return state;
    }

    IBindGroupDescriptorArena*                     arena_;
    IBindGroupDescriptorReuseMaterializer*         reuse_;
    mutable std::mutex                             mutex_;
    std::vector<BindGroupDescriptorArenaReservation> trackedReservations_;
};

enum class BindGroupDescriptorPressureLevel {
    idle,
    low,
    moderate,
    high,
    saturated,
};

enum class BindGroupDescriptorRuntimePressureAction {
    none,
    reconcile,
    retire_slot,
    release_reservations,
    throttle_growth,
};

struct BindGroupDescriptorPressureMetric {
    std::uint32_t capacity = 0;
    std::uint32_t used = 0;
    std::uint32_t available = 0;
    std::uint32_t utilizationPermille = 0;
    bool saturated = false;
};

struct BindGroupDescriptorBudgetPressure {
    BindGroupDescriptorBudget capacity;
    BindGroupDescriptorBudget used;
    BindGroupDescriptorBudget available;
    std::uint32_t utilizationPermille = 0;
    bool saturated = false;
};

struct BindGroupDescriptorArenaSlotPressure {
    std::uint32_t slotIndex = 0;
    std::uint32_t reservationCount = 0;
    BindGroupDescriptorPressureMetric bindGroupPressure;
    BindGroupDescriptorPressureMetric entryPressure;
    BindGroupDescriptorBudgetPressure budgetPressure;
    BindGroupDescriptorPressureLevel level = BindGroupDescriptorPressureLevel::idle;
};

struct BindGroupDescriptorArenaPressure {
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    BindGroupDescriptorEvictionPolicy evictionPolicy =
        BindGroupDescriptorEvictionPolicy::manual;
    std::uint32_t reservationCount = 0;
    std::uint32_t reclaimableReservationCount = 0;
    std::uint32_t reclaimableBindGroupCount = 0;
    std::uint32_t reclaimableEntryCount = 0;
    BindGroupDescriptorPressureMetric bindGroupPressure;
    BindGroupDescriptorPressureMetric entryPressure;
    BindGroupDescriptorBudgetPressure budgetPressure;
    BindGroupDescriptorPressureLevel level = BindGroupDescriptorPressureLevel::idle;
    bool canRetireSlots = false;
    bool shouldThrottleReservations = false;
    bool shouldReclaimBeforeGrowing = false;
    bool shouldRetireHottestSlot = false;
    std::optional<std::uint32_t> hottestSlotIndex;
    std::vector<BindGroupDescriptorArenaSlotPressure> slots;
};

struct BindGroupDescriptorReuseMaterializerSlotPressure {
    std::uint32_t slotIndex = 0;
    std::uint32_t activeReservationCount = 0;
    BindGroupDescriptorPressureMetric bindGroupPressure;
    BindGroupDescriptorPressureMetric entryPressure;
    BindGroupDescriptorBudgetPressure budgetPressure;
    BindGroupDescriptorPressureLevel level = BindGroupDescriptorPressureLevel::idle;
};

struct BindGroupDescriptorReuseMaterializerPressure {
    BindGroupDescriptorReuseCohortKind kind =
        BindGroupDescriptorReuseCohortKind::capacity_only;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    BindGroupDescriptorEvictionPolicy evictionPolicy =
        BindGroupDescriptorEvictionPolicy::manual;
    bool supportsLiveObjectReuse = false;
    std::uint32_t activeReservationCount = 0;
    std::uint32_t liveObjectReservationCount = 0;
    std::uint32_t capacityOnlyReservationCount = 0;
    BindGroupDescriptorPressureMetric bindGroupPressure;
    BindGroupDescriptorPressureMetric entryPressure;
    BindGroupDescriptorBudgetPressure budgetPressure;
    BindGroupDescriptorPressureLevel level = BindGroupDescriptorPressureLevel::idle;
    bool shouldThrottleRequests = false;
    bool shouldReclaimBeforeGrowing = false;
    std::optional<std::uint32_t> hottestSlotIndex;
    std::vector<BindGroupDescriptorReuseMaterializerSlotPressure> slots;
};

struct BindGroupDescriptorRuntimeCoordinatorPressure {
    bool compatible = false;
    bool drifted = false;
    bool underlyingReservationsConsistent = false;
    std::uint32_t trackedReservationCount = 0;
    std::uint32_t externalReservationCount = 0;
    std::uint32_t trackedCoveragePermille = 0;
    BindGroupDescriptorPressureLevel level = BindGroupDescriptorPressureLevel::idle;
    BindGroupDescriptorRuntimePressureAction action =
        BindGroupDescriptorRuntimePressureAction::none;
    bool shouldReconcile = false;
    bool shouldThrottleReservations = false;
    bool shouldReclaimBeforeGrowing = false;
    std::optional<std::uint32_t> reclaimSlotIndex;
    BindGroupDescriptorArenaPressure arenaPressure;
    BindGroupDescriptorReuseMaterializerPressure reusePressure;
};

enum class BindGroupDescriptorRuntimeReclamationAction {
    none,
    reconcile,
    audit_inconsistent_state,
    retire_slot,
    release_candidates,
};

struct BindGroupDescriptorReservationReclamationCandidate {
    BindGroupDescriptorArenaReservation reservation;
    std::uint32_t slotIndex = 0;
    BindGroupDescriptorPressureLevel slotPressureLevel =
        BindGroupDescriptorPressureLevel::idle;
    bool preferred = false;
};

struct BindGroupDescriptorSlotReclamationPlan {
    std::uint32_t slotIndex = 0;
    BindGroupDescriptorPressureLevel pressureLevel =
        BindGroupDescriptorPressureLevel::idle;
    std::uint32_t candidateCount = 0;
    std::uint32_t reclaimableBindGroupCount = 0;
    std::uint32_t reclaimableEntryCount = 0;
    bool recommendedForRetirement = false;
    std::vector<BindGroupDescriptorReservationReclamationCandidate> candidates;
};

struct BindGroupDescriptorRuntimeReclamationPlan {
    bool compatible = false;
    bool drifted = false;
    bool underlyingReservationsConsistent = false;
    BindGroupDescriptorRuntimeReclamationAction action =
        BindGroupDescriptorRuntimeReclamationAction::none;
    std::optional<std::uint32_t> recommendedSlotIndex;
    std::uint32_t recommendedReleaseCount = 0;
    std::uint32_t recommendedBindGroupRelief = 0;
    std::uint32_t recommendedEntryRelief = 0;
    BindGroupDescriptorRuntimeCoordinatorPressure pressure;
    std::vector<BindGroupDescriptorSlotReclamationPlan> slots;
    std::vector<BindGroupDescriptorReservationReclamationCandidate> candidates;
};

struct BindGroupDescriptorRuntimeArbitrationEntry {
    std::size_t coordinatorIndex = 0;
    std::uint32_t partitionIndex = 0;
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    BindGroupDescriptorPressureLevel level = BindGroupDescriptorPressureLevel::idle;
    BindGroupDescriptorRuntimePressureAction pressureAction =
        BindGroupDescriptorRuntimePressureAction::none;
    BindGroupDescriptorRuntimeReclamationAction reclamationAction =
        BindGroupDescriptorRuntimeReclamationAction::none;
    std::optional<std::uint32_t> recommendedSlotIndex;
    std::uint32_t recommendedReleaseCount = 0;
    std::uint32_t recommendedBindGroupRelief = 0;
    std::uint32_t recommendedEntryRelief = 0;
    bool compatible = false;
    bool drifted = false;
    bool underlyingReservationsConsistent = false;
    bool shouldThrottleReservations = false;
    bool shouldReconcile = false;
    bool shouldReclaimBeforeGrowing = false;
    bool preferred = false;
    BindGroupDescriptorRuntimeCoordinatorPressure pressure;
    BindGroupDescriptorRuntimeReclamationPlan reclamation;
};

struct BindGroupDescriptorRuntimeArbitrationPlan {
    std::uint32_t coordinatorCount = 0;
    std::uint32_t compatibleCoordinatorCount = 0;
    std::uint32_t incompatibleCoordinatorCount = 0;
    std::uint32_t driftedCoordinatorCount = 0;
    std::uint32_t reclaimingCoordinatorCount = 0;
    std::uint32_t throttledCoordinatorCount = 0;
    BindGroupDescriptorPressureLevel level = BindGroupDescriptorPressureLevel::idle;
    bool shouldReconcileBeforeGrowth = false;
    bool shouldThrottleAdmissions = false;
    bool shouldReclaimBeforeGrowth = false;
    std::optional<std::size_t> preferredCoordinatorIndex;
    BindGroupDescriptorRuntimePressureAction preferredPressureAction =
        BindGroupDescriptorRuntimePressureAction::none;
    BindGroupDescriptorRuntimeReclamationAction preferredReclamationAction =
        BindGroupDescriptorRuntimeReclamationAction::none;
    std::optional<std::uint32_t> preferredSlotIndex;
    std::vector<BindGroupDescriptorRuntimeArbitrationEntry> coordinators;
};

enum class BindGroupDescriptorRuntimeAdmissionAction {
    none,
    admit_now,
    reconcile_then_admit,
    reclaim_then_admit,
    reconcile_then_reclaim_then_admit,
    audit_before_admit,
    throttle,
    reject,
};

struct BindGroupDescriptorRuntimeAdmissionEntry {
    std::size_t coordinatorIndex = 0;
    std::uint32_t partitionIndex = 0;
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    BindGroupDescriptorRuntimeAdmissionAction action =
        BindGroupDescriptorRuntimeAdmissionAction::none;
    std::optional<BindGroupDescriptorArenaReservationRequest> request;
    std::optional<std::uint32_t> targetSlotIndex;
    std::optional<std::uint32_t> recommendedSlotIndex;
    std::uint32_t availableBindGroupCount = 0;
    std::uint32_t availableEntryCount = 0;
    std::uint32_t requiredBindGroupRelief = 0;
    std::uint32_t requiredEntryRelief = 0;
    std::uint32_t reclaimCandidateCount = 0;
    std::uint32_t reclaimableBindGroupRelief = 0;
    std::uint32_t reclaimableEntryRelief = 0;
    bool compatible = false;
    bool requestValid = false;
    bool drifted = false;
    bool underlyingReservationsConsistent = false;
    bool canAdmitNow = false;
    bool canAdmitAfterReconcile = false;
    bool canAdmitAfterReclaim = false;
    bool preferred = false;
    BindGroupDescriptorRuntimeArbitrationEntry arbitration;
};

struct BindGroupDescriptorRuntimeAdmissionPlan {
    std::uint32_t requestedBindGroupCount = 0;
    std::optional<std::uint32_t> requestedFrameIndex;
    bool requestedLiveObjectReuse = true;
    std::uint32_t coordinatorCount = 0;
    std::uint32_t compatibleCoordinatorCount = 0;
    std::uint32_t requestValidCoordinatorCount = 0;
    std::uint32_t immediateAdmissionCount = 0;
    std::uint32_t reconcileAdmissionCount = 0;
    std::uint32_t reclaimAdmissionCount = 0;
    std::uint32_t auditCoordinatorCount = 0;
    std::uint32_t throttledCoordinatorCount = 0;
    std::uint32_t rejectedCoordinatorCount = 0;
    std::uint32_t totalImmediateAvailableBindGroupCount = 0;
    std::uint32_t totalImmediateAvailableEntryCount = 0;
    std::uint32_t totalRecoverableBindGroupRelief = 0;
    std::uint32_t totalRecoverableEntryRelief = 0;
    bool shouldAttemptImmediateAdmission = false;
    bool shouldReconcileBeforeAdmission = false;
    bool shouldReclaimBeforeAdmission = false;
    bool shouldAuditBeforeAdmission = false;
    bool shouldThrottleAdmissions = false;
    BindGroupDescriptorRuntimeAdmissionAction action =
        BindGroupDescriptorRuntimeAdmissionAction::none;
    std::optional<std::size_t> preferredCoordinatorIndex;
    std::optional<std::uint32_t> preferredSlotIndex;
    BindGroupDescriptorRuntimeArbitrationPlan arbitration;
    std::vector<BindGroupDescriptorRuntimeAdmissionEntry> coordinators;
};

struct BindGroupDescriptorRuntimeBatchAdmissionIntent {
    std::uint32_t bindGroupCount = 0;
    std::optional<std::uint32_t> frameIndex;
    bool liveObjectReuse = true;
};

struct BindGroupDescriptorRuntimeBatchSlotBudget {
    std::uint32_t slotIndex = 0;
    std::uint32_t availableBindGroupCount = 0;
    std::uint32_t availableEntryCount = 0;
    std::uint32_t reclaimableBindGroupRelief = 0;
    std::uint32_t reclaimableEntryRelief = 0;
};

struct BindGroupDescriptorRuntimeBatchCoordinatorBudget {
    std::size_t coordinatorIndex = 0;
    std::uint32_t partitionIndex = 0;
    BindGroupDescriptorArenaPoolClass poolClass =
        BindGroupDescriptorArenaPoolClass::uncached_reservation;
    BindGroupDescriptorLifetimeClass lifetimeClass =
        BindGroupDescriptorLifetimeClass::retained_manual;
    bool compatible = false;
    bool drifted = false;
    bool underlyingReservationsConsistent = false;
    std::uint32_t remainingImmediateBindGroupCount = 0;
    std::uint32_t remainingImmediateEntryCount = 0;
    std::uint32_t remainingRecoverableBindGroupRelief = 0;
    std::uint32_t remainingRecoverableEntryRelief = 0;
    std::vector<BindGroupDescriptorRuntimeBatchSlotBudget> slots;
};

struct BindGroupDescriptorRuntimeBatchAdmissionDecision {
    std::size_t requestIndex = 0;
    BindGroupDescriptorRuntimeBatchAdmissionIntent request;
    bool admitted = false;
    BindGroupDescriptorRuntimeAdmissionEntry admission;
};

struct BindGroupDescriptorRuntimeBatchAdmissionPlan {
    std::uint32_t requestCount = 0;
    std::uint32_t admittedCount = 0;
    std::uint32_t immediateAdmissionCount = 0;
    std::uint32_t reconcileAdmissionCount = 0;
    std::uint32_t reclaimAdmissionCount = 0;
    std::uint32_t throttledRequestCount = 0;
    std::uint32_t auditedRequestCount = 0;
    std::uint32_t rejectedRequestCount = 0;
    std::uint32_t remainingImmediateBindGroupCount = 0;
    std::uint32_t remainingImmediateEntryCount = 0;
    std::uint32_t remainingRecoverableBindGroupRelief = 0;
    std::uint32_t remainingRecoverableEntryRelief = 0;
    bool shouldThrottleRemainingAdmissions = false;
    BindGroupDescriptorRuntimeArbitrationPlan arbitration;
    std::vector<BindGroupDescriptorRuntimeBatchCoordinatorBudget> coordinators;
    std::vector<BindGroupDescriptorRuntimeBatchAdmissionDecision> decisions;
};

[[nodiscard]] constexpr std::uint32_t bind_group_descriptor_utilization_permille(
    std::uint32_t used,
    std::uint32_t capacity) noexcept {
    if (capacity == 0) {
        return used == 0 ? 0u : 1000u;
    }
    if (used >= capacity) {
        return 1000u;
    }
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(used) * 1000u) / capacity);
}

[[nodiscard]] constexpr BindGroupDescriptorPressureLevel
bind_group_descriptor_pressure_level(std::uint32_t utilizationPermille) noexcept {
    if (utilizationPermille == 0) {
        return BindGroupDescriptorPressureLevel::idle;
    }
    if (utilizationPermille < 500) {
        return BindGroupDescriptorPressureLevel::low;
    }
    if (utilizationPermille < 800) {
        return BindGroupDescriptorPressureLevel::moderate;
    }
    if (utilizationPermille < 1000) {
        return BindGroupDescriptorPressureLevel::high;
    }
    return BindGroupDescriptorPressureLevel::saturated;
}

[[nodiscard]] constexpr BindGroupDescriptorPressureLevel
bind_group_descriptor_pressure_level_max(BindGroupDescriptorPressureLevel lhs,
                                         BindGroupDescriptorPressureLevel rhs) noexcept {
    return static_cast<int>(lhs) >= static_cast<int>(rhs) ? lhs : rhs;
}

[[nodiscard]] constexpr BindGroupDescriptorPressureMetric
bind_group_descriptor_pressure_metric(std::uint32_t used,
                                      std::uint32_t capacity) noexcept {
    BindGroupDescriptorPressureMetric metric;
    metric.capacity = capacity;
    metric.used = used;
    metric.available = capacity > used ? capacity - used : 0;
    metric.utilizationPermille =
        bind_group_descriptor_utilization_permille(used, capacity);
    metric.saturated = used >= capacity && capacity != 0;
    return metric;
}

[[nodiscard]] constexpr BindGroupDescriptorBudget
subtract_bind_group_descriptor_budget(const BindGroupDescriptorBudget& capacity,
                                      const BindGroupDescriptorBudget& used) noexcept {
    BindGroupDescriptorBudget available = capacity;
    available.totalUnits =
        capacity.totalUnits > used.totalUnits ? capacity.totalUnits - used.totalUnits : 0;
    available.bufferUnits = capacity.bufferUnits > used.bufferUnits
                                ? capacity.bufferUnits - used.bufferUnits
                                : 0;
    available.textureUnits = capacity.textureUnits > used.textureUnits
                                 ? capacity.textureUnits - used.textureUnits
                                 : 0;
    available.samplerUnits = capacity.samplerUnits > used.samplerUnits
                                 ? capacity.samplerUnits - used.samplerUnits
                                 : 0;
    return available;
}

[[nodiscard]] constexpr BindGroupDescriptorBudget
bind_group_descriptor_budget_capacity(const BindGroupDescriptorBudget& maxBudgetPerEntry,
                                      const BindGroupDescriptorBudget& totalBudget,
                                      std::uint32_t entryCapacity) noexcept {
    return bind_group_descriptor_budget_empty(totalBudget)
               ? scale_bind_group_descriptor_budget(maxBudgetPerEntry, entryCapacity)
               : totalBudget;
}

[[nodiscard]] constexpr BindGroupDescriptorBudgetPressure
bind_group_descriptor_budget_pressure(const BindGroupDescriptorBudget& capacity,
                                      const BindGroupDescriptorBudget& used) noexcept {
    BindGroupDescriptorBudgetPressure pressure;
    pressure.capacity = capacity;
    pressure.used = used;
    pressure.available = subtract_bind_group_descriptor_budget(capacity, used);
    pressure.utilizationPermille = bind_group_descriptor_utilization_permille(
        used.totalUnits, capacity.totalUnits);
    pressure.saturated =
        capacity.totalUnits != 0 && used.totalUnits >= capacity.totalUnits;
    return pressure;
}

[[nodiscard]] constexpr std::uint32_t bind_group_descriptor_slot_count(
    const SharedBindGroupDescriptorArenaMaterialization& desc) noexcept {
    return desc.requiresFrameIndex && desc.frameSlotCount != 0 ? desc.frameSlotCount : 1u;
}

[[nodiscard]] constexpr std::uint32_t bind_group_descriptor_slot_count(
    const SharedBindGroupDescriptorReuseMaterialization& desc) noexcept {
    return desc.requiresFrameIndex && desc.frameSlotCount != 0 ? desc.frameSlotCount : 1u;
}

[[nodiscard]] inline BindGroupDescriptorArenaPressure
bind_group_descriptor_arena_pressure(
    const SharedBindGroupDescriptorArenaMaterialization& desc,
    const BindGroupDescriptorArenaUsage& usage) noexcept {
    BindGroupDescriptorArenaPressure pressure;
    pressure.poolClass = desc.poolClass;
    pressure.lifetimeClass = desc.lifetimeClass;
    pressure.evictionPolicy = desc.evictionPolicy;
    pressure.reservationCount = usage.reservationCount;
    pressure.reclaimableReservationCount = usage.reservationCount;
    pressure.reclaimableBindGroupCount = usage.usedBindGroupCount;
    pressure.reclaimableEntryCount = usage.usedEntryCount;
    pressure.bindGroupPressure =
        bind_group_descriptor_pressure_metric(usage.usedBindGroupCount,
                                              saturating_multiply_u32(
                                                  desc.bindGroupCapacity,
                                                  bind_group_descriptor_slot_count(desc)));
    pressure.entryPressure =
        bind_group_descriptor_pressure_metric(usage.usedEntryCount, desc.entryCapacity);
    pressure.budgetPressure = bind_group_descriptor_budget_pressure(
        bind_group_descriptor_budget_capacity(desc.maxBudgetPerEntry,
                                             desc.totalBudget,
                                             desc.entryCapacity),
        scale_bind_group_descriptor_budget(desc.maxBudgetPerEntry, usage.usedEntryCount));
    pressure.level = bind_group_descriptor_pressure_level_max(
        bind_group_descriptor_pressure_level(pressure.bindGroupPressure.utilizationPermille),
        bind_group_descriptor_pressure_level_max(
            bind_group_descriptor_pressure_level(
                pressure.entryPressure.utilizationPermille),
            bind_group_descriptor_pressure_level(
                pressure.budgetPressure.utilizationPermille)));
    pressure.canRetireSlots =
        desc.requiresFrameIndex &&
        desc.evictionPolicy == BindGroupDescriptorEvictionPolicy::frame_retire;
    pressure.shouldThrottleReservations =
        static_cast<int>(pressure.level) >=
        static_cast<int>(BindGroupDescriptorPressureLevel::high);
    pressure.shouldReclaimBeforeGrowing =
        pressure.shouldThrottleReservations && usage.reservationCount != 0;

    std::uint32_t slotEntryCapacity =
        bind_group_descriptor_slot_count(desc) != 0
            ? desc.entryCapacity / bind_group_descriptor_slot_count(desc)
            : desc.entryCapacity;
    std::optional<BindGroupDescriptorPressureLevel> hottestSlotLevel;
    std::uint32_t hottestSlotUsedEntries = 0;
    for (const auto& slotUsage : usage.slots) {
        BindGroupDescriptorArenaSlotPressure slot;
        slot.slotIndex = slotUsage.slotIndex;
        slot.reservationCount = slotUsage.reservationCount;
        slot.bindGroupPressure = bind_group_descriptor_pressure_metric(
            slotUsage.usedBindGroupCount, desc.bindGroupCapacity);
        slot.entryPressure = bind_group_descriptor_pressure_metric(
            slotUsage.usedEntryCount, slotEntryCapacity);
        slot.budgetPressure = bind_group_descriptor_budget_pressure(
            scale_bind_group_descriptor_budget(desc.maxBudgetPerEntry, slotEntryCapacity),
            scale_bind_group_descriptor_budget(desc.maxBudgetPerEntry,
                                               slotUsage.usedEntryCount));
        slot.level = bind_group_descriptor_pressure_level_max(
            bind_group_descriptor_pressure_level(
                slot.bindGroupPressure.utilizationPermille),
            bind_group_descriptor_pressure_level_max(
                bind_group_descriptor_pressure_level(
                    slot.entryPressure.utilizationPermille),
                bind_group_descriptor_pressure_level(
                    slot.budgetPressure.utilizationPermille)));
        if (!hottestSlotLevel.has_value() ||
            static_cast<int>(slot.level) > static_cast<int>(*hottestSlotLevel) ||
            (slot.level == *hottestSlotLevel &&
             slot.entryPressure.used > hottestSlotUsedEntries)) {
            hottestSlotLevel = slot.level;
            hottestSlotUsedEntries = slot.entryPressure.used;
            pressure.hottestSlotIndex = slot.slotIndex;
        }
        pressure.slots.push_back(slot);
    }
    pressure.shouldRetireHottestSlot =
        pressure.canRetireSlots && pressure.hottestSlotIndex.has_value() &&
        static_cast<int>(pressure.slots[*pressure.hottestSlotIndex].level) >=
            static_cast<int>(BindGroupDescriptorPressureLevel::high) &&
        pressure.slots[*pressure.hottestSlotIndex].reservationCount != 0;
    return pressure;
}

[[nodiscard]] inline BindGroupDescriptorArenaPressure
bind_group_descriptor_arena_pressure(const IBindGroupDescriptorArena& arena) noexcept {
    return bind_group_descriptor_arena_pressure(arena.desc(), arena.usage());
}

[[nodiscard]] inline BindGroupDescriptorReuseMaterializerPressure
bind_group_descriptor_reuse_materializer_pressure(
    const SharedBindGroupDescriptorReuseMaterialization& desc,
    const BindGroupDescriptorReuseMaterializerState& state) noexcept {
    BindGroupDescriptorReuseMaterializerPressure pressure;
    pressure.kind = desc.kind;
    pressure.lifetimeClass = desc.lifetimeClass;
    pressure.evictionPolicy = desc.evictionPolicy;
    pressure.supportsLiveObjectReuse = desc.supportsLiveObjectReuse;
    pressure.activeReservationCount = state.activeReservationCount;
    pressure.liveObjectReservationCount = state.liveObjectReservationCount;
    pressure.capacityOnlyReservationCount = state.capacityOnlyReservationCount;
    pressure.bindGroupPressure =
        bind_group_descriptor_pressure_metric(state.activeBindGroupCount,
                                              saturating_multiply_u32(
                                                  desc.bindGroupCapacity,
                                                  bind_group_descriptor_slot_count(desc)));
    pressure.entryPressure =
        bind_group_descriptor_pressure_metric(state.activeEntryCount, desc.entryCapacity);
    pressure.budgetPressure = bind_group_descriptor_budget_pressure(
        bind_group_descriptor_budget_capacity(desc.maxBudgetPerEntry,
                                             desc.totalBudget,
                                             desc.entryCapacity),
        scale_bind_group_descriptor_budget(desc.maxBudgetPerEntry,
                                           state.activeEntryCount));
    pressure.level = bind_group_descriptor_pressure_level_max(
        bind_group_descriptor_pressure_level(pressure.bindGroupPressure.utilizationPermille),
        bind_group_descriptor_pressure_level_max(
            bind_group_descriptor_pressure_level(
                pressure.entryPressure.utilizationPermille),
            bind_group_descriptor_pressure_level(
                pressure.budgetPressure.utilizationPermille)));
    pressure.shouldThrottleRequests =
        static_cast<int>(pressure.level) >=
        static_cast<int>(BindGroupDescriptorPressureLevel::high);
    pressure.shouldReclaimBeforeGrowing =
        pressure.shouldThrottleRequests && state.activeReservationCount != 0;

    std::uint32_t slotEntryCapacity =
        bind_group_descriptor_slot_count(desc) != 0
            ? desc.entryCapacity / bind_group_descriptor_slot_count(desc)
            : desc.entryCapacity;
    std::optional<BindGroupDescriptorPressureLevel> hottestSlotLevel;
    std::uint32_t hottestSlotUsedEntries = 0;
    for (const auto& slotState : state.slots) {
        BindGroupDescriptorReuseMaterializerSlotPressure slot;
        slot.slotIndex = slotState.slotIndex;
        slot.activeReservationCount = slotState.activeReservationCount;
        slot.bindGroupPressure = bind_group_descriptor_pressure_metric(
            slotState.activeBindGroupCount, desc.bindGroupCapacity);
        slot.entryPressure = bind_group_descriptor_pressure_metric(
            slotState.activeEntryCount, slotEntryCapacity);
        slot.budgetPressure = bind_group_descriptor_budget_pressure(
            scale_bind_group_descriptor_budget(desc.maxBudgetPerEntry, slotEntryCapacity),
            scale_bind_group_descriptor_budget(desc.maxBudgetPerEntry,
                                               slotState.activeEntryCount));
        slot.level = bind_group_descriptor_pressure_level_max(
            bind_group_descriptor_pressure_level(
                slot.bindGroupPressure.utilizationPermille),
            bind_group_descriptor_pressure_level_max(
                bind_group_descriptor_pressure_level(
                    slot.entryPressure.utilizationPermille),
                bind_group_descriptor_pressure_level(
                    slot.budgetPressure.utilizationPermille)));
        if (!hottestSlotLevel.has_value() ||
            static_cast<int>(slot.level) > static_cast<int>(*hottestSlotLevel) ||
            (slot.level == *hottestSlotLevel &&
             slot.entryPressure.used > hottestSlotUsedEntries)) {
            hottestSlotLevel = slot.level;
            hottestSlotUsedEntries = slot.entryPressure.used;
            pressure.hottestSlotIndex = slot.slotIndex;
        }
        pressure.slots.push_back(slot);
    }
    return pressure;
}

[[nodiscard]] inline BindGroupDescriptorReuseMaterializerPressure
bind_group_descriptor_reuse_materializer_pressure(
    const IBindGroupDescriptorReuseMaterializer& reuse) noexcept {
    return bind_group_descriptor_reuse_materializer_pressure(reuse.desc(), reuse.state());
}

[[nodiscard]] inline BindGroupDescriptorRuntimeCoordinatorPressure
bind_group_descriptor_runtime_coordinator_pressure(
    const BindGroupDescriptorRuntimeCoordinator& coordinator) noexcept {
    const auto state = coordinator.state();
    BindGroupDescriptorRuntimeCoordinatorPressure pressure;
    pressure.compatible = state.compatible;
    pressure.drifted = state.drifted;
    pressure.underlyingReservationsConsistent =
        state.underlyingReservationsConsistent;
    pressure.trackedReservationCount = state.trackedReservationCount;
    const auto underlyingReservationCount = state.arenaUsage.reservationCount;
    pressure.externalReservationCount =
        underlyingReservationCount > state.trackedReservationCount
            ? underlyingReservationCount - state.trackedReservationCount
            : 0;
    pressure.trackedCoveragePermille = bind_group_descriptor_utilization_permille(
        state.trackedReservationCount, underlyingReservationCount);
    pressure.arenaPressure = bind_group_descriptor_arena_pressure(
        coordinator.arena().desc(), state.arenaUsage);
    pressure.reusePressure = bind_group_descriptor_reuse_materializer_pressure(
        coordinator.reuse_materializer().desc(), state.reuseState);
    pressure.level = bind_group_descriptor_pressure_level_max(
        pressure.arenaPressure.level, pressure.reusePressure.level);
    pressure.shouldReconcile = state.drifted;
    pressure.shouldThrottleReservations =
        !state.compatible || state.drifted ||
        pressure.arenaPressure.shouldThrottleReservations ||
        pressure.reusePressure.shouldThrottleRequests;
    pressure.shouldReclaimBeforeGrowing =
        !state.drifted &&
        (pressure.arenaPressure.shouldReclaimBeforeGrowing ||
         pressure.reusePressure.shouldReclaimBeforeGrowing);
    if (!state.compatible) {
        pressure.action = BindGroupDescriptorRuntimePressureAction::throttle_growth;
    } else if (state.drifted) {
        pressure.action = BindGroupDescriptorRuntimePressureAction::reconcile;
    } else if (pressure.arenaPressure.shouldRetireHottestSlot &&
               pressure.arenaPressure.hottestSlotIndex.has_value()) {
        pressure.action = BindGroupDescriptorRuntimePressureAction::retire_slot;
        pressure.reclaimSlotIndex = pressure.arenaPressure.hottestSlotIndex;
    } else if (pressure.shouldReclaimBeforeGrowing) {
        pressure.action =
            BindGroupDescriptorRuntimePressureAction::release_reservations;
    } else if (pressure.shouldThrottleReservations) {
        pressure.action = BindGroupDescriptorRuntimePressureAction::throttle_growth;
    }
    return pressure;
}

[[nodiscard]] constexpr bool bind_group_descriptor_pressure_above_target(
    std::uint32_t used,
    std::uint32_t capacity,
    std::uint32_t targetPermille = 799) noexcept {
    return bind_group_descriptor_utilization_permille(used, capacity) > targetPermille;
}

[[nodiscard]] inline BindGroupDescriptorRuntimeReclamationPlan
bind_group_descriptor_runtime_reclamation_plan(
    const BindGroupDescriptorRuntimeCoordinator& coordinator) noexcept {
    BindGroupDescriptorRuntimeReclamationPlan plan;
    plan.pressure = bind_group_descriptor_runtime_coordinator_pressure(coordinator);
    plan.compatible = plan.pressure.compatible;
    plan.drifted = plan.pressure.drifted;
    plan.underlyingReservationsConsistent =
        plan.pressure.underlyingReservationsConsistent;

    if (!plan.compatible) {
        return plan;
    }

    const auto state = coordinator.state();
    const auto& sourceReservations =
        state.underlyingReservationsConsistent ? state.arenaUsage.reservations
                                               : state.trackedReservations;

    plan.slots.reserve(plan.pressure.arenaPressure.slots.size());
    for (const auto& slotPressure : plan.pressure.arenaPressure.slots) {
        BindGroupDescriptorSlotReclamationPlan slotPlan;
        slotPlan.slotIndex = slotPressure.slotIndex;
        slotPlan.pressureLevel = slotPressure.level;
        slotPlan.recommendedForRetirement =
            plan.pressure.reclaimSlotIndex.has_value() &&
            *plan.pressure.reclaimSlotIndex == slotPressure.slotIndex;

        for (const auto& reservation : sourceReservations) {
            const auto slotIndex =
                coordinator.arena().desc().requiresFrameIndex ? reservation.frameIndex : 0u;
            if (slotIndex != slotPressure.slotIndex) {
                continue;
            }

            BindGroupDescriptorReservationReclamationCandidate candidate{
                .reservation = reservation,
                .slotIndex = slotIndex,
                .slotPressureLevel = slotPressure.level,
                .preferred = !reservation.liveObjectReuse,
            };
            slotPlan.candidates.push_back(candidate);
            slotPlan.candidateCount =
                saturating_add_u32(slotPlan.candidateCount, 1);
            slotPlan.reclaimableBindGroupCount = saturating_add_u32(
                slotPlan.reclaimableBindGroupCount, reservation.bindGroupCount);
            slotPlan.reclaimableEntryCount = saturating_add_u32(
                slotPlan.reclaimableEntryCount, reservation.entryCount);
        }

        std::stable_sort(
            slotPlan.candidates.begin(),
            slotPlan.candidates.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs.preferred != rhs.preferred) {
                    return lhs.preferred;
                }
                if (lhs.reservation.entryCount != rhs.reservation.entryCount) {
                    return lhs.reservation.entryCount > rhs.reservation.entryCount;
                }
                if (lhs.reservation.bindGroupCount != rhs.reservation.bindGroupCount) {
                    return lhs.reservation.bindGroupCount >
                           rhs.reservation.bindGroupCount;
                }
                return lhs.reservation.id < rhs.reservation.id;
            });
        plan.slots.push_back(std::move(slotPlan));
    }

    for (const auto& slotPlan : plan.slots) {
        for (const auto& candidate : slotPlan.candidates) {
            plan.candidates.push_back(candidate);
        }
    }
    std::stable_sort(
        plan.candidates.begin(),
        plan.candidates.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.slotPressureLevel != rhs.slotPressureLevel) {
                return static_cast<int>(lhs.slotPressureLevel) >
                       static_cast<int>(rhs.slotPressureLevel);
            }
            if (lhs.preferred != rhs.preferred) {
                return lhs.preferred;
            }
            if (lhs.reservation.entryCount != rhs.reservation.entryCount) {
                return lhs.reservation.entryCount > rhs.reservation.entryCount;
            }
            if (lhs.reservation.bindGroupCount != rhs.reservation.bindGroupCount) {
                return lhs.reservation.bindGroupCount >
                       rhs.reservation.bindGroupCount;
            }
            return lhs.reservation.id < rhs.reservation.id;
        });

    if (plan.drifted) {
        plan.action = plan.underlyingReservationsConsistent
                          ? BindGroupDescriptorRuntimeReclamationAction::reconcile
                          : BindGroupDescriptorRuntimeReclamationAction::
                                audit_inconsistent_state;
        return plan;
    }

    if (plan.pressure.action ==
            BindGroupDescriptorRuntimePressureAction::retire_slot &&
        plan.pressure.reclaimSlotIndex.has_value()) {
        plan.action = BindGroupDescriptorRuntimeReclamationAction::retire_slot;
        plan.recommendedSlotIndex = plan.pressure.reclaimSlotIndex;
        for (const auto& slotPlan : plan.slots) {
            if (slotPlan.slotIndex == *plan.recommendedSlotIndex) {
                plan.recommendedReleaseCount = slotPlan.candidateCount;
                plan.recommendedBindGroupRelief =
                    slotPlan.reclaimableBindGroupCount;
                plan.recommendedEntryRelief = slotPlan.reclaimableEntryCount;
                break;
            }
        }
        return plan;
    }

    if (plan.pressure.action ==
            BindGroupDescriptorRuntimePressureAction::release_reservations ||
        plan.pressure.shouldReclaimBeforeGrowing) {
        plan.action = BindGroupDescriptorRuntimeReclamationAction::release_candidates;
        auto remainingBindGroups = plan.pressure.arenaPressure.bindGroupPressure.used;
        auto remainingEntries = plan.pressure.arenaPressure.entryPressure.used;
        const auto bindGroupCapacity =
            plan.pressure.arenaPressure.bindGroupPressure.capacity;
        const auto entryCapacity = plan.pressure.arenaPressure.entryPressure.capacity;
        for (const auto& candidate : plan.candidates) {
            plan.recommendedReleaseCount =
                saturating_add_u32(plan.recommendedReleaseCount, 1);
            plan.recommendedBindGroupRelief = saturating_add_u32(
                plan.recommendedBindGroupRelief,
                candidate.reservation.bindGroupCount);
            plan.recommendedEntryRelief = saturating_add_u32(
                plan.recommendedEntryRelief, candidate.reservation.entryCount);
            remainingBindGroups = remainingBindGroups > candidate.reservation.bindGroupCount
                                      ? remainingBindGroups -
                                            candidate.reservation.bindGroupCount
                                      : 0;
            remainingEntries = remainingEntries > candidate.reservation.entryCount
                                   ? remainingEntries - candidate.reservation.entryCount
                                   : 0;
            if (!bind_group_descriptor_pressure_above_target(remainingBindGroups,
                                                            bindGroupCapacity) &&
                !bind_group_descriptor_pressure_above_target(remainingEntries,
                                                            entryCapacity)) {
                break;
            }
        }
    }

    return plan;
}

[[nodiscard]] constexpr int bind_group_descriptor_runtime_reclamation_priority(
    BindGroupDescriptorRuntimeReclamationAction action) noexcept {
    switch (action) {
    case BindGroupDescriptorRuntimeReclamationAction::audit_inconsistent_state:
        return 5;
    case BindGroupDescriptorRuntimeReclamationAction::reconcile:
        return 4;
    case BindGroupDescriptorRuntimeReclamationAction::retire_slot:
        return 3;
    case BindGroupDescriptorRuntimeReclamationAction::release_candidates:
        return 2;
    case BindGroupDescriptorRuntimeReclamationAction::none:
        return 0;
    }
    return 0;
}

[[nodiscard]] constexpr int bind_group_descriptor_runtime_pressure_priority(
    BindGroupDescriptorRuntimePressureAction action) noexcept {
    switch (action) {
    case BindGroupDescriptorRuntimePressureAction::reconcile:
        return 3;
    case BindGroupDescriptorRuntimePressureAction::retire_slot:
        return 2;
    case BindGroupDescriptorRuntimePressureAction::release_reservations:
        return 1;
    case BindGroupDescriptorRuntimePressureAction::throttle_growth:
        return 1;
    case BindGroupDescriptorRuntimePressureAction::none:
        return 0;
    }
    return 0;
}

[[nodiscard]] constexpr std::uint32_t
bind_group_descriptor_runtime_arbitration_utilization(
    const BindGroupDescriptorRuntimeCoordinatorPressure& pressure) noexcept {
    std::uint32_t utilization = pressure.arenaPressure.bindGroupPressure.utilizationPermille;
    utilization = utilization > pressure.arenaPressure.entryPressure.utilizationPermille
                      ? utilization
                      : pressure.arenaPressure.entryPressure.utilizationPermille;
    utilization = utilization > pressure.arenaPressure.budgetPressure.utilizationPermille
                      ? utilization
                      : pressure.arenaPressure.budgetPressure.utilizationPermille;
    utilization = utilization > pressure.reusePressure.bindGroupPressure.utilizationPermille
                      ? utilization
                      : pressure.reusePressure.bindGroupPressure.utilizationPermille;
    utilization = utilization > pressure.reusePressure.entryPressure.utilizationPermille
                      ? utilization
                      : pressure.reusePressure.entryPressure.utilizationPermille;
    return utilization > pressure.reusePressure.budgetPressure.utilizationPermille
               ? utilization
               : pressure.reusePressure.budgetPressure.utilizationPermille;
}

[[nodiscard]] inline BindGroupDescriptorRuntimeArbitrationPlan
bind_group_descriptor_runtime_arbitration_plan(
    std::span<const BindGroupDescriptorRuntimeCoordinator* const> coordinators) noexcept {
    BindGroupDescriptorRuntimeArbitrationPlan plan;
    plan.coordinators.reserve(coordinators.size());

    for (std::size_t i = 0; i < coordinators.size(); ++i) {
        const auto* coordinator = coordinators[i];
        if (coordinator == nullptr) {
            continue;
        }

        BindGroupDescriptorRuntimeArbitrationEntry entry;
        entry.coordinatorIndex = i;
        entry.partitionIndex = coordinator->arena().partition_index();
        entry.poolClass = coordinator->arena().pool_class();
        entry.lifetimeClass = coordinator->arena().lifetime_class();
        entry.pressure = bind_group_descriptor_runtime_coordinator_pressure(*coordinator);
        entry.reclamation =
            bind_group_descriptor_runtime_reclamation_plan(*coordinator);
        entry.level = entry.pressure.level;
        entry.pressureAction = entry.pressure.action;
        entry.reclamationAction = entry.reclamation.action;
        entry.recommendedSlotIndex = entry.reclamation.recommendedSlotIndex;
        entry.recommendedReleaseCount = entry.reclamation.recommendedReleaseCount;
        entry.recommendedBindGroupRelief =
            entry.reclamation.recommendedBindGroupRelief;
        entry.recommendedEntryRelief = entry.reclamation.recommendedEntryRelief;
        entry.compatible = entry.pressure.compatible;
        entry.drifted = entry.pressure.drifted;
        entry.underlyingReservationsConsistent =
            entry.pressure.underlyingReservationsConsistent;
        entry.shouldThrottleReservations =
            entry.pressure.shouldThrottleReservations;
        entry.shouldReconcile = entry.pressure.shouldReconcile;
        entry.shouldReclaimBeforeGrowing =
            entry.pressure.shouldReclaimBeforeGrowing;

        plan.coordinatorCount = saturating_add_u32(plan.coordinatorCount, 1);
        if (entry.compatible) {
            plan.compatibleCoordinatorCount =
                saturating_add_u32(plan.compatibleCoordinatorCount, 1);
        } else {
            plan.incompatibleCoordinatorCount =
                saturating_add_u32(plan.incompatibleCoordinatorCount, 1);
        }
        if (entry.drifted) {
            plan.driftedCoordinatorCount =
                saturating_add_u32(plan.driftedCoordinatorCount, 1);
        }
        if (entry.reclamationAction ==
                BindGroupDescriptorRuntimeReclamationAction::retire_slot ||
            entry.reclamationAction ==
                BindGroupDescriptorRuntimeReclamationAction::release_candidates) {
            plan.reclaimingCoordinatorCount =
                saturating_add_u32(plan.reclaimingCoordinatorCount, 1);
        }
        if (entry.shouldThrottleReservations || !entry.compatible) {
            plan.throttledCoordinatorCount =
                saturating_add_u32(plan.throttledCoordinatorCount, 1);
        }
        plan.level = bind_group_descriptor_pressure_level_max(plan.level, entry.level);
        plan.shouldReconcileBeforeGrowth =
            plan.shouldReconcileBeforeGrowth || entry.shouldReconcile;
        plan.shouldThrottleAdmissions =
            plan.shouldThrottleAdmissions || entry.shouldThrottleReservations ||
            !entry.compatible;
        plan.shouldReclaimBeforeGrowth =
            plan.shouldReclaimBeforeGrowth || entry.shouldReclaimBeforeGrowing;
        plan.coordinators.push_back(std::move(entry));
    }

    std::stable_sort(
        plan.coordinators.begin(),
        plan.coordinators.end(),
        [](const auto& lhs, const auto& rhs) {
            const auto lhsReclamationPriority =
                bind_group_descriptor_runtime_reclamation_priority(
                    lhs.reclamationAction);
            const auto rhsReclamationPriority =
                bind_group_descriptor_runtime_reclamation_priority(
                    rhs.reclamationAction);
            if (lhsReclamationPriority != rhsReclamationPriority) {
                return lhsReclamationPriority > rhsReclamationPriority;
            }

            const auto lhsPressurePriority =
                bind_group_descriptor_runtime_pressure_priority(lhs.pressureAction);
            const auto rhsPressurePriority =
                bind_group_descriptor_runtime_pressure_priority(rhs.pressureAction);
            if (lhsPressurePriority != rhsPressurePriority) {
                return lhsPressurePriority > rhsPressurePriority;
            }

            if (lhs.level != rhs.level) {
                return static_cast<int>(lhs.level) > static_cast<int>(rhs.level);
            }

            const auto lhsUtilization =
                bind_group_descriptor_runtime_arbitration_utilization(lhs.pressure);
            const auto rhsUtilization =
                bind_group_descriptor_runtime_arbitration_utilization(rhs.pressure);
            if (lhsUtilization != rhsUtilization) {
                return lhsUtilization > rhsUtilization;
            }

            if (lhs.recommendedEntryRelief != rhs.recommendedEntryRelief) {
                return lhs.recommendedEntryRelief > rhs.recommendedEntryRelief;
            }
            if (lhs.recommendedBindGroupRelief != rhs.recommendedBindGroupRelief) {
                return lhs.recommendedBindGroupRelief >
                       rhs.recommendedBindGroupRelief;
            }
            return lhs.coordinatorIndex < rhs.coordinatorIndex;
        });

    if (!plan.coordinators.empty()) {
        plan.coordinators.front().preferred = true;
        plan.preferredCoordinatorIndex = plan.coordinators.front().coordinatorIndex;
        plan.preferredPressureAction = plan.coordinators.front().pressureAction;
        plan.preferredReclamationAction =
            plan.coordinators.front().reclamationAction;
        plan.preferredSlotIndex = plan.coordinators.front().recommendedSlotIndex;
    }

    return plan;
}

[[nodiscard]] constexpr int bind_group_descriptor_runtime_admission_priority(
    BindGroupDescriptorRuntimeAdmissionAction action) noexcept {
    switch (action) {
    case BindGroupDescriptorRuntimeAdmissionAction::admit_now:
        return 6;
    case BindGroupDescriptorRuntimeAdmissionAction::reconcile_then_admit:
        return 5;
    case BindGroupDescriptorRuntimeAdmissionAction::reclaim_then_admit:
        return 4;
    case BindGroupDescriptorRuntimeAdmissionAction::reconcile_then_reclaim_then_admit:
        return 3;
    case BindGroupDescriptorRuntimeAdmissionAction::audit_before_admit:
        return 2;
    case BindGroupDescriptorRuntimeAdmissionAction::throttle:
        return 1;
    case BindGroupDescriptorRuntimeAdmissionAction::none:
    case BindGroupDescriptorRuntimeAdmissionAction::reject:
        return 0;
    }
    return 0;
}

[[nodiscard]] constexpr bool bind_group_descriptor_runtime_admission_requires_reconcile(
    BindGroupDescriptorRuntimeAdmissionAction action) noexcept {
    return action == BindGroupDescriptorRuntimeAdmissionAction::reconcile_then_admit ||
           action ==
               BindGroupDescriptorRuntimeAdmissionAction::
                   reconcile_then_reclaim_then_admit;
}

[[nodiscard]] constexpr bool bind_group_descriptor_runtime_admission_requires_reclaim(
    BindGroupDescriptorRuntimeAdmissionAction action) noexcept {
    return action == BindGroupDescriptorRuntimeAdmissionAction::reclaim_then_admit ||
           action ==
                BindGroupDescriptorRuntimeAdmissionAction::
                    reconcile_then_reclaim_then_admit;
}

[[nodiscard]] constexpr bool bind_group_descriptor_runtime_admission_accepts(
    BindGroupDescriptorRuntimeAdmissionAction action) noexcept {
    return action == BindGroupDescriptorRuntimeAdmissionAction::admit_now ||
           action ==
               BindGroupDescriptorRuntimeAdmissionAction::reconcile_then_admit ||
           action ==
               BindGroupDescriptorRuntimeAdmissionAction::reclaim_then_admit ||
           action ==
               BindGroupDescriptorRuntimeAdmissionAction::
                   reconcile_then_reclaim_then_admit;
}

[[nodiscard]] inline const BindGroupDescriptorArenaSlotUsage*
bind_group_descriptor_find_arena_slot_usage(
    const BindGroupDescriptorArenaUsage& usage,
    std::uint32_t                        slotIndex) noexcept {
    for (const auto& slot : usage.slots) {
        if (slot.slotIndex == slotIndex) {
            return &slot;
        }
    }
    return nullptr;
}

[[nodiscard]] inline const BindGroupDescriptorSlotReclamationPlan*
bind_group_descriptor_find_slot_reclamation_plan(
    const BindGroupDescriptorRuntimeReclamationPlan& plan,
    std::uint32_t                                    slotIndex) noexcept {
    for (const auto& slot : plan.slots) {
        if (slot.slotIndex == slotIndex) {
            return &slot;
        }
    }
    return nullptr;
}

[[nodiscard]] inline bool bind_group_descriptor_runtime_admission_entry_better(
    const BindGroupDescriptorRuntimeAdmissionEntry& lhs,
    const BindGroupDescriptorRuntimeAdmissionEntry& rhs) noexcept {
    const auto lhsPriority =
        bind_group_descriptor_runtime_admission_priority(lhs.action);
    const auto rhsPriority =
        bind_group_descriptor_runtime_admission_priority(rhs.action);
    if (lhsPriority != rhsPriority) {
        return lhsPriority > rhsPriority;
    }

    if (lhs.requiredEntryRelief != rhs.requiredEntryRelief) {
        return lhs.requiredEntryRelief < rhs.requiredEntryRelief;
    }
    if (lhs.requiredBindGroupRelief != rhs.requiredBindGroupRelief) {
        return lhs.requiredBindGroupRelief < rhs.requiredBindGroupRelief;
    }
    if (lhs.availableEntryCount != rhs.availableEntryCount) {
        return lhs.availableEntryCount > rhs.availableEntryCount;
    }
    if (lhs.availableBindGroupCount != rhs.availableBindGroupCount) {
        return lhs.availableBindGroupCount > rhs.availableBindGroupCount;
    }

    const auto lhsUtilization =
        bind_group_descriptor_runtime_arbitration_utilization(lhs.arbitration.pressure);
    const auto rhsUtilization =
        bind_group_descriptor_runtime_arbitration_utilization(rhs.arbitration.pressure);
    if (lhsUtilization != rhsUtilization) {
        return lhsUtilization < rhsUtilization;
    }

    if (lhs.arbitration.level != rhs.arbitration.level) {
        return static_cast<int>(lhs.arbitration.level) <
               static_cast<int>(rhs.arbitration.level);
    }

    const auto lhsReclamationPriority =
        bind_group_descriptor_runtime_reclamation_priority(
            lhs.arbitration.reclamationAction);
    const auto rhsReclamationPriority =
        bind_group_descriptor_runtime_reclamation_priority(
            rhs.arbitration.reclamationAction);
    if (lhsReclamationPriority != rhsReclamationPriority) {
        return lhsReclamationPriority < rhsReclamationPriority;
    }

    if (lhs.reclaimCandidateCount != rhs.reclaimCandidateCount) {
        return lhs.reclaimCandidateCount < rhs.reclaimCandidateCount;
    }
    return lhs.coordinatorIndex < rhs.coordinatorIndex;
}

[[nodiscard]] inline BindGroupDescriptorRuntimeBatchCoordinatorBudget*
bind_group_descriptor_find_batch_coordinator_budget(
    std::vector<BindGroupDescriptorRuntimeBatchCoordinatorBudget>& budgets,
    std::size_t                                                  coordinatorIndex) noexcept {
    for (auto& budget : budgets) {
        if (budget.coordinatorIndex == coordinatorIndex) {
            return &budget;
        }
    }
    return nullptr;
}

[[nodiscard]] inline BindGroupDescriptorRuntimeBatchSlotBudget*
bind_group_descriptor_find_batch_slot_budget(
    BindGroupDescriptorRuntimeBatchCoordinatorBudget& budget,
    std::uint32_t                                     slotIndex) noexcept {
    for (auto& slot : budget.slots) {
        if (slot.slotIndex == slotIndex) {
            return &slot;
        }
    }
    return nullptr;
}

inline void bind_group_descriptor_runtime_batch_budget_refresh(
    BindGroupDescriptorRuntimeBatchCoordinatorBudget& budget) noexcept {
    budget.remainingImmediateBindGroupCount = 0;
    budget.remainingImmediateEntryCount = 0;
    budget.remainingRecoverableBindGroupRelief = 0;
    budget.remainingRecoverableEntryRelief = 0;
    for (const auto& slot : budget.slots) {
        budget.remainingImmediateBindGroupCount = saturating_add_u32(
            budget.remainingImmediateBindGroupCount,
            slot.availableBindGroupCount);
        budget.remainingImmediateEntryCount = saturating_add_u32(
            budget.remainingImmediateEntryCount, slot.availableEntryCount);
        budget.remainingRecoverableBindGroupRelief = saturating_add_u32(
            budget.remainingRecoverableBindGroupRelief,
            slot.reclaimableBindGroupRelief);
        budget.remainingRecoverableEntryRelief = saturating_add_u32(
            budget.remainingRecoverableEntryRelief,
            slot.reclaimableEntryRelief);
    }
}

[[nodiscard]] inline BindGroupDescriptorRuntimeAdmissionPlan
bind_group_descriptor_runtime_admission_plan(
    std::span<const BindGroupDescriptorRuntimeCoordinator* const> coordinators,
    std::uint32_t                                                bindGroupCount,
    std::optional<std::uint32_t> frameIndex = std::nullopt,
    bool liveObjectReuse = true) noexcept {
    BindGroupDescriptorRuntimeAdmissionPlan plan;
    plan.requestedBindGroupCount = bindGroupCount;
    plan.requestedFrameIndex = frameIndex;
    plan.requestedLiveObjectReuse = liveObjectReuse;
    plan.arbitration = bind_group_descriptor_runtime_arbitration_plan(coordinators);
    plan.coordinatorCount = plan.arbitration.coordinatorCount;
    plan.coordinators.reserve(plan.arbitration.coordinators.size());

    for (const auto& arbitrationEntry : plan.arbitration.coordinators) {
        if (arbitrationEntry.coordinatorIndex >= coordinators.size()) {
            continue;
        }

        const auto* coordinator = coordinators[arbitrationEntry.coordinatorIndex];
        if (coordinator == nullptr) {
            continue;
        }

        BindGroupDescriptorRuntimeAdmissionEntry entry;
        entry.coordinatorIndex = arbitrationEntry.coordinatorIndex;
        entry.partitionIndex = arbitrationEntry.partitionIndex;
        entry.poolClass = arbitrationEntry.poolClass;
        entry.lifetimeClass = arbitrationEntry.lifetimeClass;
        entry.recommendedSlotIndex = arbitrationEntry.recommendedSlotIndex;
        entry.compatible = arbitrationEntry.compatible;
        entry.drifted = arbitrationEntry.drifted;
        entry.underlyingReservationsConsistent =
            arbitrationEntry.underlyingReservationsConsistent;
        entry.arbitration = arbitrationEntry;

        if (entry.compatible) {
            plan.compatibleCoordinatorCount =
                saturating_add_u32(plan.compatibleCoordinatorCount, 1);
        }

        const auto request =
            coordinator->make_reservation_request(bindGroupCount,
                                                  frameIndex,
                                                  liveObjectReuse);
        if (request.ok()) {
            const auto& resolvedRequest = request.value();
            entry.requestValid = true;
            entry.request = resolvedRequest;
            plan.requestValidCoordinatorCount =
                saturating_add_u32(plan.requestValidCoordinatorCount, 1);

            const auto targetSlotIndex =
                coordinator->arena().desc().requiresFrameIndex ? resolvedRequest.frameIndex
                                                               : 0u;
            entry.targetSlotIndex = targetSlotIndex;

            const auto state = coordinator->state();
            if (const auto* slotUsage =
                    bind_group_descriptor_find_arena_slot_usage(state.arenaUsage,
                                                                targetSlotIndex)) {
                entry.availableBindGroupCount = slotUsage->availableBindGroupCount;
                entry.availableEntryCount = slotUsage->availableEntryCount;
            } else {
                entry.availableBindGroupCount =
                    state.arenaUsage.availableBindGroupCount;
                entry.availableEntryCount = state.arenaUsage.availableEntryCount;
            }

            entry.requiredBindGroupRelief =
                entry.availableBindGroupCount >= resolvedRequest.bindGroupCount
                    ? 0u
                    : resolvedRequest.bindGroupCount - entry.availableBindGroupCount;
            entry.requiredEntryRelief =
                entry.availableEntryCount >= resolvedRequest.entryCount
                    ? 0u
                    : resolvedRequest.entryCount - entry.availableEntryCount;

            if (const auto* slotPlan =
                    bind_group_descriptor_find_slot_reclamation_plan(
                        arbitrationEntry.reclamation, targetSlotIndex)) {
                entry.reclaimCandidateCount = slotPlan->candidateCount;
                entry.reclaimableBindGroupRelief =
                    slotPlan->reclaimableBindGroupCount;
                entry.reclaimableEntryRelief = slotPlan->reclaimableEntryCount;
            }

            const bool arenaCanReserve =
                coordinator->arena().can_reserve(resolvedRequest);
            entry.canAdmitNow = !entry.drifted && arenaCanReserve;
            entry.canAdmitAfterReconcile =
                entry.drifted && entry.underlyingReservationsConsistent &&
                arenaCanReserve;

            const bool slotReliefNeeded = entry.requiredBindGroupRelief != 0 ||
                                          entry.requiredEntryRelief != 0;
            const bool slotReliefSufficient =
                slotReliefNeeded &&
                entry.reclaimableBindGroupRelief >= entry.requiredBindGroupRelief &&
                entry.reclaimableEntryRelief >= entry.requiredEntryRelief;
            entry.canAdmitAfterReclaim =
                slotReliefSufficient &&
                (!entry.drifted || entry.underlyingReservationsConsistent);
        }

        if (!entry.compatible || !entry.requestValid) {
            entry.action = BindGroupDescriptorRuntimeAdmissionAction::reject;
        } else if (entry.canAdmitNow) {
            entry.action = BindGroupDescriptorRuntimeAdmissionAction::admit_now;
        } else if (entry.canAdmitAfterReconcile) {
            entry.action =
                BindGroupDescriptorRuntimeAdmissionAction::reconcile_then_admit;
        } else if (entry.drifted && !entry.underlyingReservationsConsistent) {
            entry.action =
                BindGroupDescriptorRuntimeAdmissionAction::audit_before_admit;
        } else if (entry.drifted && entry.canAdmitAfterReclaim) {
            entry.action = BindGroupDescriptorRuntimeAdmissionAction::
                reconcile_then_reclaim_then_admit;
        } else if (entry.canAdmitAfterReclaim) {
            entry.action =
                BindGroupDescriptorRuntimeAdmissionAction::reclaim_then_admit;
        } else if (entry.arbitration.shouldThrottleReservations ||
                   entry.arbitration.shouldReconcile ||
                   entry.arbitration.shouldReclaimBeforeGrowing) {
            entry.action = BindGroupDescriptorRuntimeAdmissionAction::throttle;
        } else {
            entry.action = BindGroupDescriptorRuntimeAdmissionAction::reject;
        }

        if (entry.action == BindGroupDescriptorRuntimeAdmissionAction::admit_now) {
            plan.immediateAdmissionCount =
                saturating_add_u32(plan.immediateAdmissionCount, 1);
            plan.totalImmediateAvailableBindGroupCount = saturating_add_u32(
                plan.totalImmediateAvailableBindGroupCount,
                entry.availableBindGroupCount);
            plan.totalImmediateAvailableEntryCount = saturating_add_u32(
                plan.totalImmediateAvailableEntryCount, entry.availableEntryCount);
        }
        if (bind_group_descriptor_runtime_admission_requires_reconcile(
                entry.action)) {
            plan.reconcileAdmissionCount =
                saturating_add_u32(plan.reconcileAdmissionCount, 1);
            plan.shouldReconcileBeforeAdmission = true;
        }
        if (bind_group_descriptor_runtime_admission_requires_reclaim(
                entry.action)) {
            plan.reclaimAdmissionCount =
                saturating_add_u32(plan.reclaimAdmissionCount, 1);
            plan.shouldReclaimBeforeAdmission = true;
            plan.totalRecoverableBindGroupRelief = saturating_add_u32(
                plan.totalRecoverableBindGroupRelief,
                entry.reclaimableBindGroupRelief);
            plan.totalRecoverableEntryRelief = saturating_add_u32(
                plan.totalRecoverableEntryRelief,
                entry.reclaimableEntryRelief);
        }
        if (entry.action ==
            BindGroupDescriptorRuntimeAdmissionAction::audit_before_admit) {
            plan.auditCoordinatorCount =
                saturating_add_u32(plan.auditCoordinatorCount, 1);
            plan.shouldAuditBeforeAdmission = true;
        }
        if (entry.action == BindGroupDescriptorRuntimeAdmissionAction::throttle) {
            plan.throttledCoordinatorCount =
                saturating_add_u32(plan.throttledCoordinatorCount, 1);
            plan.shouldThrottleAdmissions = true;
        }
        if (entry.action == BindGroupDescriptorRuntimeAdmissionAction::reject) {
            plan.rejectedCoordinatorCount =
                saturating_add_u32(plan.rejectedCoordinatorCount, 1);
        }
        plan.coordinators.push_back(std::move(entry));
    }

    plan.shouldAttemptImmediateAdmission = plan.immediateAdmissionCount != 0;

    std::stable_sort(
        plan.coordinators.begin(),
        plan.coordinators.end(),
        bind_group_descriptor_runtime_admission_entry_better);

    if (!plan.coordinators.empty()) {
        plan.coordinators.front().preferred = true;
        plan.action = plan.coordinators.front().action;
        plan.preferredCoordinatorIndex =
            plan.coordinators.front().coordinatorIndex;
        plan.preferredSlotIndex = plan.coordinators.front().targetSlotIndex;
    }

    return plan;
}

[[nodiscard]] inline BindGroupDescriptorRuntimeBatchAdmissionPlan
bind_group_descriptor_runtime_batch_admission_plan(
    std::span<const BindGroupDescriptorRuntimeCoordinator* const> coordinators,
    std::span<const BindGroupDescriptorRuntimeBatchAdmissionIntent> requests) noexcept {
    BindGroupDescriptorRuntimeBatchAdmissionPlan plan;
    plan.requestCount = static_cast<std::uint32_t>(requests.size());
    plan.arbitration = bind_group_descriptor_runtime_arbitration_plan(coordinators);
    plan.coordinators.reserve(plan.arbitration.coordinators.size());
    plan.decisions.reserve(requests.size());

    for (const auto& arbitrationEntry : plan.arbitration.coordinators) {
        if (arbitrationEntry.coordinatorIndex >= coordinators.size()) {
            continue;
        }
        const auto* coordinator = coordinators[arbitrationEntry.coordinatorIndex];
        if (coordinator == nullptr) {
            continue;
        }

        BindGroupDescriptorRuntimeBatchCoordinatorBudget budget;
        budget.coordinatorIndex = arbitrationEntry.coordinatorIndex;
        budget.partitionIndex = arbitrationEntry.partitionIndex;
        budget.poolClass = arbitrationEntry.poolClass;
        budget.lifetimeClass = arbitrationEntry.lifetimeClass;
        budget.compatible = arbitrationEntry.compatible;
        budget.drifted = arbitrationEntry.drifted;
        budget.underlyingReservationsConsistent =
            arbitrationEntry.underlyingReservationsConsistent;

        const auto state = coordinator->state();
        const auto reclamation =
            bind_group_descriptor_runtime_reclamation_plan(*coordinator);
        budget.slots.reserve(state.arenaUsage.slots.size());
        for (const auto& slotUsage : state.arenaUsage.slots) {
            BindGroupDescriptorRuntimeBatchSlotBudget slot;
            slot.slotIndex = slotUsage.slotIndex;
            slot.availableBindGroupCount = slotUsage.availableBindGroupCount;
            slot.availableEntryCount = slotUsage.availableEntryCount;
            if (const auto* slotPlan = bind_group_descriptor_find_slot_reclamation_plan(
                    reclamation,
                    slotUsage.slotIndex)) {
                slot.reclaimableBindGroupRelief =
                    slotPlan->reclaimableBindGroupCount;
                slot.reclaimableEntryRelief =
                    slotPlan->reclaimableEntryCount;
            }
            budget.slots.push_back(std::move(slot));
        }
        bind_group_descriptor_runtime_batch_budget_refresh(budget);
        plan.coordinators.push_back(std::move(budget));
    }

    for (std::size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex) {
        const auto& request = requests[requestIndex];
        BindGroupDescriptorRuntimeBatchAdmissionDecision decision;
        decision.requestIndex = requestIndex;
        decision.request = request;

        auto baselinePlan = bind_group_descriptor_runtime_admission_plan(
            coordinators,
            request.bindGroupCount,
            request.frameIndex,
            request.liveObjectReuse);
        std::vector<BindGroupDescriptorRuntimeAdmissionEntry> candidates;
        candidates.reserve(baselinePlan.coordinators.size());

        for (auto baselineEntry : baselinePlan.coordinators) {
            auto* budget = bind_group_descriptor_find_batch_coordinator_budget(
                plan.coordinators,
                baselineEntry.coordinatorIndex);
            if (budget == nullptr) {
                continue;
            }

            baselineEntry.compatible = budget->compatible;
            baselineEntry.drifted = budget->drifted;
            baselineEntry.underlyingReservationsConsistent =
                budget->underlyingReservationsConsistent;
            baselineEntry.arbitration.compatible = baselineEntry.compatible;
            baselineEntry.arbitration.drifted = baselineEntry.drifted;
            baselineEntry.arbitration.underlyingReservationsConsistent =
                baselineEntry.underlyingReservationsConsistent;
            baselineEntry.arbitration.shouldReconcile = baselineEntry.drifted;

            if (!baselineEntry.request.has_value() ||
                !baselineEntry.targetSlotIndex.has_value()) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::reject;
                candidates.push_back(std::move(baselineEntry));
                continue;
            }

            auto* slotBudget = bind_group_descriptor_find_batch_slot_budget(
                *budget,
                *baselineEntry.targetSlotIndex);
            if (slotBudget == nullptr) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::reject;
                candidates.push_back(std::move(baselineEntry));
                continue;
            }

            const auto& resolvedRequest = *baselineEntry.request;
            baselineEntry.availableBindGroupCount = slotBudget->availableBindGroupCount;
            baselineEntry.availableEntryCount = slotBudget->availableEntryCount;
            baselineEntry.reclaimableBindGroupRelief =
                slotBudget->reclaimableBindGroupRelief;
            baselineEntry.reclaimableEntryRelief =
                slotBudget->reclaimableEntryRelief;
            baselineEntry.reclaimCandidateCount =
                baselineEntry.reclaimableBindGroupRelief != 0 ||
                        baselineEntry.reclaimableEntryRelief != 0
                    ? 1u
                    : 0u;
            baselineEntry.requiredBindGroupRelief =
                baselineEntry.availableBindGroupCount >= resolvedRequest.bindGroupCount
                    ? 0u
                    : resolvedRequest.bindGroupCount -
                          baselineEntry.availableBindGroupCount;
            baselineEntry.requiredEntryRelief =
                baselineEntry.availableEntryCount >= resolvedRequest.entryCount
                    ? 0u
                    : resolvedRequest.entryCount - baselineEntry.availableEntryCount;

            const bool canAdmitNow =
                !baselineEntry.drifted &&
                baselineEntry.availableBindGroupCount >=
                    resolvedRequest.bindGroupCount &&
                baselineEntry.availableEntryCount >= resolvedRequest.entryCount;
            const bool canAdmitAfterReconcile =
                baselineEntry.drifted &&
                baselineEntry.underlyingReservationsConsistent &&
                baselineEntry.availableBindGroupCount >=
                    resolvedRequest.bindGroupCount &&
                baselineEntry.availableEntryCount >= resolvedRequest.entryCount;
            const bool needsRelief =
                baselineEntry.requiredBindGroupRelief != 0 ||
                baselineEntry.requiredEntryRelief != 0;
            const bool canAdmitAfterReclaim =
                needsRelief &&
                baselineEntry.reclaimableBindGroupRelief >=
                    baselineEntry.requiredBindGroupRelief &&
                baselineEntry.reclaimableEntryRelief >=
                    baselineEntry.requiredEntryRelief &&
                (!baselineEntry.drifted ||
                 baselineEntry.underlyingReservationsConsistent);

            if (!baselineEntry.compatible) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::reject;
            } else if (canAdmitNow) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::admit_now;
            } else if (canAdmitAfterReconcile) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::
                        reconcile_then_admit;
            } else if (baselineEntry.drifted &&
                       !baselineEntry.underlyingReservationsConsistent) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::
                        audit_before_admit;
            } else if (baselineEntry.drifted && canAdmitAfterReclaim) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::
                        reconcile_then_reclaim_then_admit;
            } else if (canAdmitAfterReclaim) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::
                        reclaim_then_admit;
            } else if (baselineEntry.arbitration.shouldThrottleReservations ||
                       baselineEntry.arbitration.shouldReclaimBeforeGrowing ||
                       baselineEntry.arbitration.shouldReconcile) {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::throttle;
            } else {
                baselineEntry.action =
                    BindGroupDescriptorRuntimeAdmissionAction::reject;
            }
            candidates.push_back(std::move(baselineEntry));
        }

        std::stable_sort(candidates.begin(),
                         candidates.end(),
                         bind_group_descriptor_runtime_admission_entry_better);
        if (!candidates.empty()) {
            candidates.front().preferred = true;
            decision.admission = candidates.front();
            decision.admitted = bind_group_descriptor_runtime_admission_accepts(
                decision.admission.action);
        } else {
            decision.admission.action =
                BindGroupDescriptorRuntimeAdmissionAction::reject;
        }

        if (decision.admitted && decision.admission.targetSlotIndex.has_value()) {
            auto* budget = bind_group_descriptor_find_batch_coordinator_budget(
                plan.coordinators,
                decision.admission.coordinatorIndex);
            if (budget != nullptr) {
                if (auto* slotBudget = bind_group_descriptor_find_batch_slot_budget(
                        *budget,
                        *decision.admission.targetSlotIndex)) {
                    const auto& resolvedRequest = *decision.admission.request;
                    auto consume_capacity =
                        [](std::uint32_t& available,
                           std::uint32_t& reclaimable,
                           std::uint32_t  requested) noexcept {
                            if (available >= requested) {
                                available -= requested;
                                return;
                            }
                            const auto deficit = requested - available;
                            available = 0;
                            reclaimable = reclaimable > deficit
                                              ? reclaimable - deficit
                                              : 0u;
                        };
                    consume_capacity(slotBudget->availableBindGroupCount,
                                     slotBudget->reclaimableBindGroupRelief,
                                     resolvedRequest.bindGroupCount);
                    consume_capacity(slotBudget->availableEntryCount,
                                     slotBudget->reclaimableEntryRelief,
                                     resolvedRequest.entryCount);
                    if (bind_group_descriptor_runtime_admission_requires_reconcile(
                            decision.admission.action)) {
                        budget->drifted = false;
                    }
                    bind_group_descriptor_runtime_batch_budget_refresh(*budget);
                }
            }
        }

        if (decision.admitted) {
            plan.admittedCount = saturating_add_u32(plan.admittedCount, 1);
            if (decision.admission.action ==
                BindGroupDescriptorRuntimeAdmissionAction::admit_now) {
                plan.immediateAdmissionCount =
                    saturating_add_u32(plan.immediateAdmissionCount, 1);
            }
            if (bind_group_descriptor_runtime_admission_requires_reconcile(
                    decision.admission.action)) {
                plan.reconcileAdmissionCount =
                    saturating_add_u32(plan.reconcileAdmissionCount, 1);
            }
            if (bind_group_descriptor_runtime_admission_requires_reclaim(
                    decision.admission.action)) {
                plan.reclaimAdmissionCount =
                    saturating_add_u32(plan.reclaimAdmissionCount, 1);
            }
        } else if (decision.admission.action ==
                   BindGroupDescriptorRuntimeAdmissionAction::throttle) {
            plan.throttledRequestCount =
                saturating_add_u32(plan.throttledRequestCount, 1);
            plan.shouldThrottleRemainingAdmissions = true;
        } else if (decision.admission.action ==
                   BindGroupDescriptorRuntimeAdmissionAction::audit_before_admit) {
            plan.auditedRequestCount =
                saturating_add_u32(plan.auditedRequestCount, 1);
        } else {
            plan.rejectedRequestCount =
                saturating_add_u32(plan.rejectedRequestCount, 1);
        }

        plan.decisions.push_back(std::move(decision));
    }

    for (const auto& budget : plan.coordinators) {
        plan.remainingImmediateBindGroupCount = saturating_add_u32(
            plan.remainingImmediateBindGroupCount,
            budget.remainingImmediateBindGroupCount);
        plan.remainingImmediateEntryCount = saturating_add_u32(
            plan.remainingImmediateEntryCount,
            budget.remainingImmediateEntryCount);
        plan.remainingRecoverableBindGroupRelief = saturating_add_u32(
            plan.remainingRecoverableBindGroupRelief,
            budget.remainingRecoverableBindGroupRelief);
        plan.remainingRecoverableEntryRelief = saturating_add_u32(
            plan.remainingRecoverableEntryRelief,
            budget.remainingRecoverableEntryRelief);
    }

    return plan;
}

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
    // Optional render-pass-scoped stencil reference. Built-in backends require an
    // active render pass whose depth attachment format has a stencil aspect.
    [[nodiscard]] virtual core::Status set_stencil_reference(
        std::uint32_t reference) {
        (void)reference;
        return core::Status::failure(
            core::StatusCode::unsupported,
            "dynamic stencil reference is not supported by this command buffer");
    }

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
    [[nodiscard]] virtual core::Result<std::unique_ptr<IBindGroupDescriptorArena>>
    create_bind_group_descriptor_arena(
        const SharedBindGroupDescriptorArenaMaterialization& desc) {
        (void)desc;
        return core::Status::failure(
            core::StatusCode::unsupported,
            "descriptor arena allocation is not supported by this device");
    }
    [[nodiscard]] virtual core::Result<
        std::unique_ptr<IBindGroupDescriptorReuseMaterializer>>
    create_bind_group_descriptor_reuse_materializer(
        const SharedBindGroupDescriptorReuseMaterialization& desc) {
        (void)desc;
        return core::Status::failure(
            core::StatusCode::unsupported,
            "descriptor reuse materializers are not supported by this device");
    }
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
    report.descriptorMappingModel = caps.descriptorPolicy.mappingModel;
    report.descriptorAllocationModel = caps.descriptorPolicy.allocationModel;
    report.descriptorUpdateModel = caps.descriptorPolicy.updateModel;
    report.descriptorBudgetModel = caps.descriptorPolicy.budgetModel;
    report.flattenedNativeBindings =
        caps.descriptorPolicy.flattenedNativeBindings;
    for (const auto& heap : caps.memoryHeaps) {
        report.memoryBudgetBytes += heap.budgetBytes;
        report.dedicatedMemoryHeap = report.dedicatedMemoryHeap || heap.dedicated;
    }
    return report;
}

} // namespace truffle::rhi
