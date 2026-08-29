#pragma once

#include "truffle/rhi/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace truffle::rhi {

class Buffer;
class Texture;
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

enum class MemoryDomain { upload, readback, device_local };
enum class IndexFormat { uint16, uint32 };
enum class TextureFormat {
    unknown,
    rgba8_unorm,
    rgba8_srgb,
    bgra8_unorm,
    depth32_float,
    depth32_float_stencil8,
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
enum class CommandListState { initial, recording, executable, submitted, invalid };
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
};

struct DeviceDesc {
    std::string debugName;
    std::vector<Feature> requiredFeatures;
    std::vector<Feature> optionalFeatures;
};

struct BufferDesc {
    std::size_t size = 0;
    BufferUsage usage = BufferUsage::none;
    MemoryDomain memory = MemoryDomain::device_local;
    bool mappedAtCreation = false;
    std::string debugName;
};

struct TextureDesc {
    Extent3D extent{};
    TextureFormat format = TextureFormat::rgba8_unorm;
    TextureUsage usage = TextureUsage::sampled;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    std::uint32_t sampleCount = 1;
    std::string debugName;
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
    std::uint64_t texturesCreated = 0;
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
};

} // namespace truffle::rhi
