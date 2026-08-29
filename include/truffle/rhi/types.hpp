#pragma once

#include "truffle/rhi/status.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace truffle::rhi {

class Buffer;
class BufferView;
class Texture;
class TextureView;
class Shader;
class Sampler;
class BindGroupLayout;
class BindGroup;
class BindlessTable;
class DescriptorArena;
class PipelineLayout;
class PipelineCache;
class CommandList;
class Fence;
class Semaphore;
class Swapchain;

struct ObjectId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] friend constexpr bool operator==(ObjectId, ObjectId) = default;
};

enum class BackendKind {
    null_validation,
    metal,
    vulkan,
    direct3d12,
    opengl,
    webgpu,
    opengles,
    webgl2,
};

enum class QueueKind { graphics, compute, transfer };

enum class Feature {
    presentation,
    compute,
    transfer,
    timestamp_queries,
    memory_budget,
    external_memory,
    descriptor_arrays,
    dynamic_offsets,
    push_constants,
    bindless_descriptors,
    indirect_count,
    pipeline_cache,
};

enum class BufferUsage : std::uint32_t {
    none = 0,
    vertex = 1u << 0u,
    index = 1u << 1u,
    uniform = 1u << 2u,
    storage = 1u << 3u,
    indirect = 1u << 4u,
    copy_source = 1u << 5u,
    copy_destination = 1u << 6u,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage lhs,
                                               BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(lhs) |
                                    static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr BufferUsage operator&(BufferUsage lhs,
                                               BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(lhs) &
                                    static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool has_usage(BufferUsage value,
                                       BufferUsage required) noexcept {
    return (value & required) == required;
}

enum class TextureUsage : std::uint32_t {
    none = 0,
    sampled = 1u << 0u,
    storage = 1u << 1u,
    color_attachment = 1u << 2u,
    depth_stencil_attachment = 1u << 3u,
    copy_source = 1u << 4u,
    copy_destination = 1u << 5u,
    present = 1u << 6u,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage lhs,
                                                TextureUsage rhs) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(lhs) |
                                     static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr TextureUsage operator&(TextureUsage lhs,
                                                TextureUsage rhs) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(lhs) &
                                     static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool has_usage(TextureUsage value,
                                       TextureUsage required) noexcept {
    return (value & required) == required;
}

enum class MemoryDomain { upload, readback, device_local, external };
enum class IndexFormat { uint16, uint32 };
enum class TextureDimension { d1, d2, d3, cube };
enum class TextureAspect : std::uint32_t {
    none = 0,
    color = 1u << 0u,
    depth = 1u << 1u,
    stencil = 1u << 2u,
};

[[nodiscard]] constexpr TextureAspect operator|(TextureAspect lhs,
                                                 TextureAspect rhs) noexcept {
    return static_cast<TextureAspect>(static_cast<std::uint32_t>(lhs) |
                                      static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr TextureAspect operator&(TextureAspect lhs,
                                                 TextureAspect rhs) noexcept {
    return static_cast<TextureAspect>(static_cast<std::uint32_t>(lhs) &
                                      static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool has_aspect(TextureAspect value,
                                        TextureAspect required) noexcept {
    return (value & required) == required;
}

enum class TextureFormat {
    unknown,
    r8_unorm,
    rg8_unorm,
    rgba8_unorm,
    rgba8_srgb,
    bgra8_unorm,
    bgra8_srgb,
    rgba16_float,
    rgba32_float,
    depth16_unorm,
    depth24_unorm_stencil8,
    depth32_float,
    depth32_float_stencil8,
    bc1_rgba_unorm,
    bc1_rgba_srgb,
    bc3_rgba_unorm,
    bc3_rgba_srgb,
};
enum class ShaderStage { vertex, fragment, compute };
enum class ShaderStageMask : std::uint32_t {
    none = 0,
    vertex = 1u << 0u,
    fragment = 1u << 1u,
    compute = 1u << 2u,
    all_graphics = (1u << 0u) | (1u << 1u),
    all = (1u << 0u) | (1u << 1u) | (1u << 2u),
};

[[nodiscard]] constexpr ShaderStageMask operator|(ShaderStageMask lhs,
                                                   ShaderStageMask rhs) noexcept {
    return static_cast<ShaderStageMask>(static_cast<std::uint32_t>(lhs) |
                                        static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr ShaderStageMask operator&(ShaderStageMask lhs,
                                                   ShaderStageMask rhs) noexcept {
    return static_cast<ShaderStageMask>(static_cast<std::uint32_t>(lhs) &
                                        static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool has_stage(ShaderStageMask value,
                                       ShaderStageMask required) noexcept {
    return (value & required) == required;
}

[[nodiscard]] constexpr ShaderStageMask shader_stage_mask(
    ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::vertex:
        return ShaderStageMask::vertex;
    case ShaderStage::fragment:
        return ShaderStageMask::fragment;
    case ShaderStage::compute:
        return ShaderStageMask::compute;
    }
    return ShaderStageMask::none;
}
enum class ShaderByteFormat {
    native_source,
    spirv,
    dxil,
    metal_library,
};
enum class ShaderValueType : std::uint8_t {
    boolean,
    sint32,
    uint32,
    float32,
};
enum class PrimitiveTopology {
    triangle_list,
    triangle_strip,
    line_list,
    point_list,
    patch_list,
};
enum class PolygonMode { fill, line, point };
enum class CullMode { none, front, back };
enum class FrontFace { counter_clockwise, clockwise };
enum class CompareOp {
    never,
    less,
    equal,
    less_equal,
    greater,
    not_equal,
    greater_equal,
    always,
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
enum class BlendOp { add, subtract, reverse_subtract, minimum, maximum };
enum class VertexStepMode { vertex, instance };
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
enum class DynamicState : std::uint32_t {
    none = 0,
    viewport = 1u << 0u,
    scissor = 1u << 1u,
    blend_constant = 1u << 2u,
    stencil_reference = 1u << 3u,
    depth_bias = 1u << 4u,
};

[[nodiscard]] constexpr DynamicState operator|(DynamicState lhs,
                                                DynamicState rhs) noexcept {
    return static_cast<DynamicState>(static_cast<std::uint32_t>(lhs) |
                                     static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr DynamicState operator&(DynamicState lhs,
                                                DynamicState rhs) noexcept {
    return static_cast<DynamicState>(static_cast<std::uint32_t>(lhs) &
                                     static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool has_dynamic_state(DynamicState value,
                                                DynamicState required) noexcept {
    return (value & required) == required;
}
enum class SamplerAddressMode { clamp_to_edge, repeat, mirror_repeat };
enum class LoadOp { load, clear, dont_care };
enum class StoreOp { store, dont_care };
enum class PresentMode { fifo, mailbox, immediate };
enum class PipelineStage : std::uint64_t {
    none = 0,
    top = 1ull << 0u,
    draw_indirect = 1ull << 1u,
    vertex_input = 1ull << 2u,
    vertex_shader = 1ull << 3u,
    fragment_shader = 1ull << 4u,
    early_fragment_tests = 1ull << 5u,
    late_fragment_tests = 1ull << 6u,
    color_attachment_output = 1ull << 7u,
    compute_shader = 1ull << 8u,
    copy = 1ull << 9u,
    bottom = 1ull << 10u,
    host = 1ull << 11u,
    all_graphics = draw_indirect | vertex_input | vertex_shader |
                   fragment_shader | early_fragment_tests |
                   late_fragment_tests | color_attachment_output,
    all_commands = all_graphics | compute_shader | copy,
};

[[nodiscard]] constexpr PipelineStage operator|(PipelineStage lhs,
                                                 PipelineStage rhs) noexcept {
    return static_cast<PipelineStage>(static_cast<std::uint64_t>(lhs) |
                                      static_cast<std::uint64_t>(rhs));
}

[[nodiscard]] constexpr PipelineStage operator&(PipelineStage lhs,
                                                 PipelineStage rhs) noexcept {
    return static_cast<PipelineStage>(static_cast<std::uint64_t>(lhs) &
                                      static_cast<std::uint64_t>(rhs));
}

[[nodiscard]] constexpr bool has_pipeline_stage(
    PipelineStage value, PipelineStage required) noexcept {
    return (value & required) == required;
}

enum class Access : std::uint64_t {
    none = 0,
    indirect_read = 1ull << 0u,
    index_read = 1ull << 1u,
    vertex_attribute_read = 1ull << 2u,
    uniform_read = 1ull << 3u,
    shader_read = 1ull << 4u,
    shader_write = 1ull << 5u,
    color_attachment_read = 1ull << 6u,
    color_attachment_write = 1ull << 7u,
    depth_stencil_read = 1ull << 8u,
    depth_stencil_write = 1ull << 9u,
    transfer_read = 1ull << 10u,
    transfer_write = 1ull << 11u,
    host_read = 1ull << 12u,
    host_write = 1ull << 13u,
    memory_read = 1ull << 14u,
    memory_write = 1ull << 15u,
};

[[nodiscard]] constexpr Access operator|(Access lhs, Access rhs) noexcept {
    return static_cast<Access>(static_cast<std::uint64_t>(lhs) |
                               static_cast<std::uint64_t>(rhs));
}

[[nodiscard]] constexpr Access operator&(Access lhs, Access rhs) noexcept {
    return static_cast<Access>(static_cast<std::uint64_t>(lhs) &
                               static_cast<std::uint64_t>(rhs));
}

[[nodiscard]] constexpr bool has_access(Access value, Access required) noexcept {
    return (value & required) == required;
}

enum class TextureLayout {
    undefined,
    general,
    color_attachment,
    depth_stencil_attachment,
    depth_stencil_read_only,
    shader_read_only,
    transfer_source,
    transfer_destination,
    present,
};
enum class Filter { nearest, linear };
enum class CommandListState { initial, recording, executable, submitted, invalid };
enum class ExternalHandleType {
    none,
    opaque_pointer,
    opaque_file_descriptor,
    win32_handle,
    metal_shared_event,
};
enum class NativeSurfaceKind {
    headless,
    cocoa_layer,
    win32,
    wayland,
    xcb,
    android_window,
    web_canvas,
    external,
};

enum class ResourceBindingType { buffer, texture, sampler };
enum class BindingType {
    uniform_buffer,
    storage_buffer,
    sampled_texture,
    storage_texture,
    sampler,
};

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Extent2D&) const = default;
};

struct Extent3D {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;

    bool operator==(const Extent3D&) const = default;
};

struct Origin3D {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
};

struct Viewport {
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
    float minimumDepth = 0.0F;
    float maximumDepth = 1.0F;
};

struct ScissorRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
};

struct TextureSubresource {
    TextureAspect aspect = TextureAspect::color;
    std::uint32_t mipLevel = 0;
    std::uint32_t arrayLayer = 0;
};

struct TextureSubresourceRange {
    TextureAspect aspects = TextureAspect::color;
    std::uint32_t baseMipLevel = 0;
    std::uint32_t mipLevelCount = 1;
    std::uint32_t baseArrayLayer = 0;
    std::uint32_t arrayLayerCount = 1;
};

struct TextureRegion {
    TextureSubresource subresource;
    Origin3D origin;
    Extent3D extent;
};

struct TextureDataLayout {
    std::size_t offset = 0;
    std::size_t bytesPerRow = 0;
    std::size_t rowsPerImage = 0;
};

struct BufferCopyRegion {
    std::size_t sourceOffset = 0;
    std::size_t destinationOffset = 0;
    std::size_t size = 0;
};

struct BufferTextureCopyRegion {
    std::size_t bufferOffset = 0;
    TextureDataLayout layout;
    TextureRegion texture;
};

struct TextureCopyRegion {
    TextureRegion source;
    TextureRegion destination;
};

struct TextureBlitRegion {
    TextureRegion source;
    TextureRegion destination;
    Filter filter = Filter::nearest;
};

struct ResourceBinding {
    std::string name;
    ShaderStage stage = ShaderStage::vertex;
    ResourceBindingType type = ResourceBindingType::buffer;
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    std::uint32_t arrayCount = 1;
    std::size_t minimumSize = 0;
    bool readOnly = true;
};

class PipelineReflection {
public:
    PipelineReflection() = default;
    explicit PipelineReflection(std::vector<ResourceBinding> bindings)
        : bindings_(std::move(bindings)) {}

    [[nodiscard]] std::span<const ResourceBinding> bindings() const noexcept {
        return bindings_;
    }

    [[nodiscard]] const ResourceBinding* find(std::uint32_t group,
                                              std::uint32_t binding,
                                              ShaderStage stage) const noexcept;

private:
    std::vector<ResourceBinding> bindings_;
};

struct InstanceDesc {
    bool enableValidation = true;
    void (*debugCallback)(const BackendDiagnostic&, void*) = nullptr;
    void* debugUserData = nullptr;
};

struct AdapterInfo {
    std::string name;
    BackendKind backend = BackendKind::null_validation;
    bool native = false;
    bool validationOnly = false;
    bool presentation = false;
    std::vector<QueueKind> queueKinds;
    std::vector<Feature> supportedFeatures;
    struct ResourceCapabilities {
        bool bufferViews = false;
        bool textureViews = false;
        bool hostCoherent = false;
        bool bufferCopy = false;
        bool bufferFill = false;
        bool bufferTextureCopy = false;
        bool textureCopy = false;
        bool textureClear = false;
        bool textureResolve = false;
        bool textureBlitNearest = false;
        bool textureBlitLinear = false;
        bool externalImport = false;
        bool externalExport = false;
    } resources;
    struct BindingCapabilities {
        bool ordinaryBindGroups = false;
        bool descriptorArrays = false;
        bool dynamicOffsets = false;
        bool immutableSamplers = false;
        bool pushConstants = false;
        bool bindlessTables = false;
        bool updateAfterBind = false;
        std::uint32_t maxBindGroups = 0;
        std::uint32_t maxBindingsPerGroup = 0;
        std::uint32_t maxDescriptorsPerGroup = 0;
        std::uint32_t maxPushConstantBytes = 0;
        std::uint32_t minUniformBufferOffsetAlignment = 1;
        std::uint32_t minStorageBufferOffsetAlignment = 1;
    } bindings;
    struct PipelineCapabilities {
        bool graphics = false;
        bool compute = false;
        bool multipleRenderTargets = false;
        bool depthStencil = false;
        bool multisample = false;
        bool tessellation = false;
        bool indirect = false;
        bool indirectCount = false;
        bool pipelineCache = false;
        std::uint32_t maxColorAttachments = 0;
        std::uint32_t maxVertexBuffers = 0;
        std::uint32_t maxViewports = 0;
        Extent3D maxComputeWorkgroupSize{0, 0, 0};
        std::uint32_t maxComputeInvocations = 0;
    } pipelines;
};

struct ResourceAllocatorCallbacks {
    void* userData = nullptr;
    bool (*reserve)(MemoryDomain, std::size_t, std::size_t, void*) = nullptr;
    void (*release)(MemoryDomain, std::size_t, std::size_t, void*) = nullptr;
};

struct DeviceDesc {
    std::string debugName;
    std::vector<Feature> requiredFeatures;
    std::vector<Feature> optionalFeatures;
    ResourceAllocatorCallbacks allocator;
};

struct BufferDesc {
    std::size_t size = 0;
    BufferUsage usage = BufferUsage::none;
    MemoryDomain memory = MemoryDomain::device_local;
    bool mappedAtCreation = false;
    bool shareable = false;
    std::string debugName;
};

inline constexpr std::size_t whole_size =
    std::numeric_limits<std::size_t>::max();

struct BufferViewDesc {
    std::size_t offset = 0;
    std::size_t size = whole_size;
    std::size_t stride = 0;
    std::string debugName;
};

struct TextureDesc {
    TextureDimension dimension = TextureDimension::d2;
    Extent3D extent{};
    TextureFormat format = TextureFormat::rgba8_unorm;
    TextureUsage usage = TextureUsage::sampled;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    std::uint32_t sampleCount = 1;
    MemoryDomain memory = MemoryDomain::device_local;
    bool shareable = false;
    std::string debugName;
};

struct TextureViewDesc {
    TextureDimension dimension = TextureDimension::d2;
    TextureFormat format = TextureFormat::unknown;
    TextureSubresourceRange range;
    std::string debugName;
};

struct MemoryRequirements {
    std::size_t size = 0;
    std::size_t alignment = 1;
};

struct MemoryBudget {
    std::size_t budgetBytes = 0;
    std::size_t usedBytes = 0;

    [[nodiscard]] std::size_t available_bytes() const noexcept {
        return usedBytes < budgetBytes ? budgetBytes - usedBytes : 0;
    }
};

struct ExternalMemoryHandle {
    ExternalHandleType type = ExternalHandleType::none;
    std::uint64_t value = 0;

    [[nodiscard]] bool valid() const noexcept {
        return type != ExternalHandleType::none && value != 0;
    }
};

struct ShaderSpecializationConstant {
    std::uint32_t id = 0;
    std::string name;
    ShaderValueType type = ShaderValueType::uint32;
    std::uint32_t defaultValueBits = 0;

    bool operator==(const ShaderSpecializationConstant&) const = default;
};

struct ShaderBindingMap {
    ShaderStage stage = ShaderStage::vertex;
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    std::uint32_t arrayElement = 0;
    std::uint32_t nativeGroup = 0;
    std::uint32_t nativeBinding = 0;
    std::uint32_t nativeArrayElement = 0;
};

struct PushConstantRange {
    ShaderStage stage = ShaderStage::vertex;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;

    bool operator==(const PushConstantRange&) const = default;
};

struct ShaderDesc {
    ShaderStage stage = ShaderStage::vertex;
    ShaderByteFormat format = ShaderByteFormat::native_source;
    std::string entryPoint = "main";
    std::vector<std::byte> code;
    std::vector<ResourceBinding> reflection;
    std::vector<PushConstantRange> pushConstants;
    std::vector<ShaderSpecializationConstant> specializationConstants;
    std::vector<ShaderBindingMap> bindingMap;
    Extent3D requiredWorkgroupSize{1, 1, 1};
    Extent3D preferredWorkgroupSize{1, 1, 1};
    std::string debugName;
};

struct SamplerDesc {
    Filter minFilter = Filter::nearest;
    Filter magFilter = Filter::nearest;
    Filter mipFilter = Filter::nearest;
    SamplerAddressMode addressU = SamplerAddressMode::clamp_to_edge;
    SamplerAddressMode addressV = SamplerAddressMode::clamp_to_edge;
    SamplerAddressMode addressW = SamplerAddressMode::clamp_to_edge;
    float lodMin = 0.0F;
    float lodMax = 32.0F;
    float maxAnisotropy = 1.0F;
    CompareOp compare = CompareOp::always;
    std::string debugName;
};

struct BindGroupLayoutEntry {
    std::uint32_t binding = 0;
    BindingType type = BindingType::uniform_buffer;
    std::uint32_t arrayCount = 1;
    ShaderStageMask visibility = ShaderStageMask::all;
    bool dynamicOffset = false;
    std::size_t minimumBufferSize = 0;
    Sampler* immutableSampler = nullptr;
};

struct BindGroupLayoutDesc {
    std::uint32_t group = 0;
    std::vector<BindGroupLayoutEntry> entries;
    std::string debugName;
};

struct DescriptorArenaDesc {
    std::uint32_t maxBindGroups = 256;
    std::uint32_t maxDescriptors = 4096;
    bool updateAfterBind = false;
    std::string debugName;
};

struct BindGroupEntry {
    std::uint32_t binding = 0;
    std::uint32_t arrayElement = 0;
    Buffer* buffer = nullptr;
    std::size_t offset = 0;
    std::size_t size = whole_size;
    TextureView* textureView = nullptr;
    Sampler* sampler = nullptr;
};

struct BindGroupDesc {
    BindGroupLayout* layout = nullptr;
    DescriptorArena* arena = nullptr;
    std::vector<BindGroupEntry> entries;
    std::string debugName;
};

struct BindlessTableDesc {
    BindGroupLayout* layout = nullptr;
    std::uint32_t capacity = 0;
    std::string debugName;
};

struct PipelineLayoutDesc {
    std::vector<BindGroupLayout*> bindGroupLayouts;
    std::vector<PushConstantRange> pushConstants;
    std::string debugName;
};

struct PipelineCacheDesc {
    std::vector<std::byte> initialData;
    std::string debugName;
};

struct SpecializationValue {
    std::uint32_t id = 0;
    ShaderValueType type = ShaderValueType::uint32;
    std::uint32_t valueBits = 0;
};

struct VertexAttributeDesc {
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::float32x4;
    std::uint32_t offset = 0;
};

struct VertexBufferLayoutDesc {
    std::uint32_t stride = 0;
    VertexStepMode stepMode = VertexStepMode::vertex;
    std::vector<VertexAttributeDesc> attributes;
};

struct BlendComponent {
    BlendFactor sourceFactor = BlendFactor::one;
    BlendFactor destinationFactor = BlendFactor::zero;
    BlendOp operation = BlendOp::add;
};

struct BlendState {
    bool enabled = false;
    BlendComponent color;
    BlendComponent alpha;
};

struct ColorTargetState {
    TextureFormat format = TextureFormat::bgra8_unorm;
    BlendState blend;
    std::uint8_t writeMask = 0x0f;
};

struct StencilFaceState {
    CompareOp compare = CompareOp::always;
    StencilOp failOp = StencilOp::keep;
    StencilOp depthFailOp = StencilOp::keep;
    StencilOp passOp = StencilOp::keep;
};

struct DepthStencilState {
    TextureFormat format = TextureFormat::unknown;
    bool depthWriteEnabled = false;
    CompareOp depthCompare = CompareOp::always;
    StencilFaceState front;
    StencilFaceState back;
    std::uint32_t stencilReadMask = 0xffffffffu;
    std::uint32_t stencilWriteMask = 0xffffffffu;
};

struct RasterizationState {
    PolygonMode polygonMode = PolygonMode::fill;
    CullMode cullMode = CullMode::none;
    FrontFace frontFace = FrontFace::counter_clockwise;
    bool depthClampEnabled = false;
    float depthBias = 0.0F;
    float depthBiasSlopeScale = 0.0F;
    float depthBiasClamp = 0.0F;
};

struct MultisampleState {
    std::uint32_t sampleCount = 1;
    std::uint32_t sampleMask = 0xffffffffu;
    bool alphaToCoverageEnabled = false;
};

struct PipelineDesc {
    Shader* vertexShader = nullptr;
    Shader* fragmentShader = nullptr;
    PipelineLayout* layout = nullptr;
    PipelineCache* cache = nullptr;
    std::vector<VertexBufferLayoutDesc> vertexBuffers;
    std::vector<ColorTargetState> colorTargets;
    DepthStencilState depthStencil;
    RasterizationState rasterization;
    MultisampleState multisample;
    PrimitiveTopology topology = PrimitiveTopology::triangle_list;
    std::uint32_t patchControlPoints = 0;
    std::vector<Viewport> viewports;
    std::vector<ScissorRect> scissors;
    std::array<float, 4> blendConstant{};
    std::uint32_t stencilReference = 0;
    DynamicState dynamicState = DynamicState::none;
    std::vector<SpecializationValue> specializationConstants;
    std::string debugName;
};

struct ComputePipelineDesc {
    Shader* computeShader = nullptr;
    PipelineLayout* layout = nullptr;
    PipelineCache* cache = nullptr;
    Extent3D requiredWorkgroupSize{0, 0, 0};
    Extent3D preferredWorkgroupSize{0, 0, 0};
    std::vector<SpecializationValue> specializationConstants;
    std::string debugName;
};

struct NativeSurface {
    NativeSurfaceKind kind = NativeSurfaceKind::headless;
    void* handle = nullptr;
    void* display = nullptr;
};

struct SurfaceDesc {
    NativeSurface native;
    Extent2D initialExtent{1, 1};
    std::string debugName;
};

struct SwapchainDesc {
    Extent2D extent{1, 1};
    TextureFormat format = TextureFormat::bgra8_unorm;
    PresentMode presentMode = PresentMode::fifo;
    std::uint32_t imageCount = 2;
    std::string debugName;
};

struct ClearColor {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

struct ClearValue {
    ClearColor color;
    float depth = 1.0F;
    std::uint32_t stencil = 0;
};

struct ColorAttachmentDesc {
    Texture* texture = nullptr;
    Texture* resolveTexture = nullptr;
    LoadOp loadOp = LoadOp::clear;
    StoreOp storeOp = StoreOp::store;
    ClearColor clear{};
};

struct DepthStencilAttachmentDesc {
    Texture* texture = nullptr;
    LoadOp depthLoadOp = LoadOp::clear;
    StoreOp depthStoreOp = StoreOp::store;
    float clearDepth = 1.0F;
    LoadOp stencilLoadOp = LoadOp::clear;
    StoreOp stencilStoreOp = StoreOp::store;
    std::uint32_t clearStencil = 0;
};

struct RenderPassDesc {
    Extent2D extent{1, 1};
    std::vector<ColorAttachmentDesc> colorAttachments;
    DepthStencilAttachmentDesc depthStencilAttachment;
};

struct FenceDesc {
    std::uint64_t initialValue = 0;
    std::string debugName;
};

struct SemaphoreDesc {
    std::uint64_t initialValue = 0;
    std::string debugName;
};

struct BufferBarrier {
    Buffer* buffer = nullptr;
    std::size_t offset = 0;
    std::size_t size = whole_size;
    PipelineStage sourceStages = PipelineStage::top;
    PipelineStage destinationStages = PipelineStage::bottom;
    Access sourceAccess = Access::none;
    Access destinationAccess = Access::none;
    bool transferOwnership = false;
    QueueKind sourceQueue = QueueKind::graphics;
    QueueKind destinationQueue = QueueKind::graphics;
};

struct TextureBarrier {
    Texture* texture = nullptr;
    TextureSubresourceRange range;
    TextureLayout oldLayout = TextureLayout::undefined;
    TextureLayout newLayout = TextureLayout::general;
    PipelineStage sourceStages = PipelineStage::top;
    PipelineStage destinationStages = PipelineStage::bottom;
    Access sourceAccess = Access::none;
    Access destinationAccess = Access::none;
    bool transferOwnership = false;
    QueueKind sourceQueue = QueueKind::graphics;
    QueueKind destinationQueue = QueueKind::graphics;
};

struct AliasingBarrier {
    Buffer* beforeBuffer = nullptr;
    Texture* beforeTexture = nullptr;
    Buffer* afterBuffer = nullptr;
    Texture* afterTexture = nullptr;
    PipelineStage sourceStages = PipelineStage::all_commands;
    PipelineStage destinationStages = PipelineStage::all_commands;
};

struct BarrierBatch {
    std::vector<BufferBarrier> buffers;
    std::vector<TextureBarrier> textures;
    std::vector<AliasingBarrier> aliasing;
};

struct SemaphoreWait {
    Semaphore* semaphore = nullptr;
    std::uint64_t value = 0;
    PipelineStage stages = PipelineStage::all_commands;
};

struct SemaphoreSignal {
    Semaphore* semaphore = nullptr;
    std::uint64_t value = 0;
};

struct QueueSubmitDesc {
    std::span<CommandList* const> commandLists;
    std::span<const SemaphoreWait> waits;
    std::span<const SemaphoreSignal> signals;
    Fence* signalFence = nullptr;
    std::uint64_t signalFenceValue = 0;
    std::chrono::nanoseconds waitTimeout = std::chrono::nanoseconds::max();
};

struct QueuePresentDesc {
    Swapchain* swapchain = nullptr;
    std::uint32_t imageIndex = 0;
    std::span<const SemaphoreWait> waits;
};

enum class QueryType { timestamp, occlusion, pipeline_statistics };

struct QueryPoolDesc {
    QueryType type = QueryType::timestamp;
    std::uint32_t count = 1;
    std::string debugName;
};

struct BackendStats {
    std::uint64_t devicesCreated = 0;
    std::uint64_t buffersCreated = 0;
    std::uint64_t bufferViewsCreated = 0;
    std::uint64_t texturesCreated = 0;
    std::uint64_t textureViewsCreated = 0;
    std::uint64_t samplersCreated = 0;
    std::uint64_t bindGroupLayoutsCreated = 0;
    std::uint64_t descriptorArenasCreated = 0;
    std::uint64_t bindGroupsCreated = 0;
    std::uint64_t pipelineLayoutsCreated = 0;
    std::uint64_t pipelineCachesCreated = 0;
    std::uint64_t shadersCreated = 0;
    std::uint64_t pipelinesCreated = 0;
    std::uint64_t commandPoolsCreated = 0;
    std::uint64_t commandListsCreated = 0;
    std::uint64_t surfacesCreated = 0;
    std::uint64_t swapchainsCreated = 0;
    std::uint64_t drawsRecorded = 0;
    std::uint64_t dispatchesRecorded = 0;
    std::uint64_t submissions = 0;
    std::uint64_t presentations = 0;
    std::uint64_t transfersExecuted = 0;
};

} // namespace truffle::rhi
