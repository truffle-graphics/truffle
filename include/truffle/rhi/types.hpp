#pragma once

#include "truffle/rhi/status.hpp"

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
enum class ShaderByteFormat {
    native_source,
    spirv,
    dxil,
    metal_library,
};
enum class PrimitiveTopology { triangle_list, triangle_strip, line_list, point_list };
enum class LoadOp { load, clear, dont_care };
enum class StoreOp { store, dont_care };
enum class PresentMode { fifo, mailbox, immediate };
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

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct Extent3D {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
};

struct Origin3D {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
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

struct ShaderDesc {
    ShaderStage stage = ShaderStage::vertex;
    ShaderByteFormat format = ShaderByteFormat::native_source;
    std::string entryPoint = "main";
    std::vector<std::byte> code;
    std::vector<ResourceBinding> reflection;
    std::string debugName;
};

struct PipelineDesc {
    Shader* vertexShader = nullptr;
    Shader* fragmentShader = nullptr;
    TextureFormat colorFormat = TextureFormat::bgra8_unorm;
    TextureFormat depthStencilFormat = TextureFormat::unknown;
    PrimitiveTopology topology = PrimitiveTopology::triangle_list;
    std::string debugName;
};

struct ComputePipelineDesc {
    Shader* computeShader = nullptr;
    Extent3D preferredWorkgroupSize{64, 1, 1};
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
    LoadOp loadOp = LoadOp::clear;
    StoreOp storeOp = StoreOp::store;
    ClearColor clear{};
};

struct RenderPassDesc {
    Extent2D extent{1, 1};
    std::vector<ColorAttachmentDesc> colorAttachments;
};

struct FenceDesc {
    std::uint64_t initialValue = 0;
    std::string debugName;
};

struct SemaphoreDesc {
    std::uint64_t initialValue = 0;
    std::string debugName;
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
