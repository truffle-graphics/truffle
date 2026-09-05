#include "truffle/rhi/vulkan_backend.hpp"

#include "foundation_backend.hpp"

#include <volk.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace truffle::rhi {
namespace {

constexpr auto portability_subset_extension = "VK_KHR_portability_subset";

[[nodiscard]] Status vulkan_failure(StatusCode code, std::string message,
                                    VkResult result) {
    BackendDiagnostic detail{
        .domain = "vulkan",
        .nativeCode = static_cast<std::int64_t>(result),
        .objectLabel = {},
        .message = message,
    };
    return Status::failure(code, std::move(message), std::move(detail));
}

struct VulkanContext {
    ~VulkanContext() {
        if (device != VK_NULL_HANDLE) {
            deviceTable.vkDeviceWaitIdle(device);
            deviceTable.vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            instanceTable.vkDestroyInstance(instance, nullptr);
        }
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VolkInstanceTable instanceTable{};
    VolkDeviceTable deviceTable{};
    std::mutex mutex;
};

struct VulkanBufferResource {
    VulkanBufferResource(std::shared_ptr<VulkanContext> contextValue,
                         VkBuffer bufferValue, VkDeviceMemory memoryValue,
                         VkDeviceSize allocationSizeValue,
                         std::size_t logicalSizeValue, bool hostVisibleValue,
                         bool hostCoherentValue, void* mappedValue)
        : context(std::move(contextValue)), buffer(bufferValue),
          memory(memoryValue), allocationSize(allocationSizeValue),
          logicalSize(logicalSizeValue), hostVisible(hostVisibleValue),
          hostCoherent(hostCoherentValue), mapped(mappedValue) {}

    ~VulkanBufferResource() {
        if (!context || context->device == VK_NULL_HANDLE) {
            return;
        }
        std::lock_guard contextLock{context->mutex};
        std::lock_guard resourceLock{mutex};
        if (mapped != nullptr) {
            context->deviceTable.vkUnmapMemory(context->device, memory);
            mapped = nullptr;
        }
        if (buffer != VK_NULL_HANDLE) {
            context->deviceTable.vkDestroyBuffer(context->device, buffer,
                                                  nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            context->deviceTable.vkFreeMemory(context->device, memory, nullptr);
        }
    }

    std::shared_ptr<VulkanContext> context;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocationSize = 0;
    std::size_t logicalSize = 0;
    bool hostVisible = false;
    bool hostCoherent = false;
    void* mapped = nullptr;
    std::mutex mutex;
};

struct VulkanShaderResource {
    VulkanShaderResource(std::shared_ptr<VulkanContext> contextValue,
                         VkShaderModule moduleValue, ShaderDesc descValue)
        : context(std::move(contextValue)), module(moduleValue),
          desc(std::move(descValue)) {}

    ~VulkanShaderResource() {
        if (!context || context->device == VK_NULL_HANDLE ||
            module == VK_NULL_HANDLE) {
            return;
        }
        std::lock_guard lock{context->mutex};
        context->deviceTable.vkDestroyShaderModule(context->device, module,
                                                    nullptr);
    }

    VulkanShaderResource(const VulkanShaderResource&) = delete;
    VulkanShaderResource& operator=(const VulkanShaderResource&) = delete;

    std::shared_ptr<VulkanContext> context;
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderDesc desc;
};

struct VulkanSamplerResource {
    VulkanSamplerResource(std::shared_ptr<VulkanContext> contextValue,
                          VkSampler samplerValue)
        : context(std::move(contextValue)), sampler(samplerValue) {}

    ~VulkanSamplerResource() {
        if (!context || context->device == VK_NULL_HANDLE ||
            sampler == VK_NULL_HANDLE) {
            return;
        }
        std::lock_guard lock{context->mutex};
        context->deviceTable.vkDestroySampler(context->device, sampler,
                                               nullptr);
    }

    std::shared_ptr<VulkanContext> context;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanPipelineResource {
    VulkanPipelineResource(
        std::shared_ptr<VulkanContext> contextValue,
        std::vector<VkDescriptorSetLayout> setLayoutsValue,
        VkPipelineLayout layoutValue, VkPipeline pipelineValue,
        VkPipelineBindPoint bindPointValue,
        VkRenderPass renderPassValue = VK_NULL_HANDLE,
        bool automaticViewportValue = false,
        bool automaticScissorValue = false)
        : context(std::move(contextValue)),
          setLayouts(std::move(setLayoutsValue)), layout(layoutValue),
          pipeline(pipelineValue), bindPoint(bindPointValue),
          renderPass(renderPassValue),
          automaticViewport(automaticViewportValue),
          automaticScissor(automaticScissorValue) {}

    ~VulkanPipelineResource() {
        if (!context || context->device == VK_NULL_HANDLE) {
            return;
        }
        std::lock_guard lock{context->mutex};
        if (pipeline != VK_NULL_HANDLE) {
            context->deviceTable.vkDestroyPipeline(context->device, pipeline,
                                                    nullptr);
        }
        if (layout != VK_NULL_HANDLE) {
            context->deviceTable.vkDestroyPipelineLayout(context->device,
                                                          layout, nullptr);
        }
        if (renderPass != VK_NULL_HANDLE) {
            context->deviceTable.vkDestroyRenderPass(context->device,
                                                     renderPass, nullptr);
        }
        for (const auto setLayout : setLayouts) {
            context->deviceTable.vkDestroyDescriptorSetLayout(
                context->device, setLayout, nullptr);
        }
    }

    std::shared_ptr<VulkanContext> context;
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool automaticViewport = false;
    bool automaticScissor = false;
};

struct VulkanFormat {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspects = 0;
    std::uint32_t blockWidth = 1;
    std::uint32_t blockHeight = 1;
    std::uint32_t bytesPerBlock = 0;
    bool compressed = false;
};

[[nodiscard]] VulkanFormat vulkan_format(TextureFormat format) {
    switch (format) {
    case TextureFormat::r8_unorm:
        return {VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 1};
    case TextureFormat::rg8_unorm:
        return {VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 2};
    case TextureFormat::rgba8_unorm:
        return {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 4};
    case TextureFormat::rgba8_srgb:
        return {VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 4};
    case TextureFormat::bgra8_unorm:
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 4};
    case TextureFormat::bgra8_srgb:
        return {VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 4};
    case TextureFormat::rgba16_float:
        return {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                8};
    case TextureFormat::rgba32_float:
        return {VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                16};
    case TextureFormat::depth16_unorm:
        return {VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1, 2};
    case TextureFormat::depth24_unorm_stencil8:
        return {VK_FORMAT_D24_UNORM_S8_UINT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1, 1,
                4};
    case TextureFormat::depth32_float:
        return {VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1, 4};
    case TextureFormat::depth32_float_stencil8:
        return {VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1, 1,
                8};
    case TextureFormat::bc1_rgba_unorm:
        return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 4,
                8, true};
    case TextureFormat::bc1_rgba_srgb:
        return {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 4,
                8, true};
    case TextureFormat::bc3_rgba_unorm:
        return {VK_FORMAT_BC3_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 4, 16,
                true};
    case TextureFormat::bc3_rgba_srgb:
        return {VK_FORMAT_BC3_SRGB_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 4, 16,
                true};
    case TextureFormat::unknown:
        break;
    }
    return {};
}

[[nodiscard]] constexpr std::uint32_t divide_round_up(
    std::uint32_t value, std::uint32_t divisor) noexcept {
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] constexpr std::uint32_t mip_dimension(
    std::uint32_t value, std::uint32_t mipLevel) noexcept {
    return std::max(1u, value >> mipLevel);
}

[[nodiscard]] constexpr VkImageType vulkan_image_type(
    TextureDimension dimension) noexcept {
    switch (dimension) {
    case TextureDimension::d1:
        return VK_IMAGE_TYPE_1D;
    case TextureDimension::d2:
    case TextureDimension::cube:
        return VK_IMAGE_TYPE_2D;
    case TextureDimension::d3:
        return VK_IMAGE_TYPE_3D;
    }
    return VK_IMAGE_TYPE_MAX_ENUM;
}

[[nodiscard]] constexpr VkSampleCountFlagBits vulkan_sample_count(
    std::uint32_t sampleCount) noexcept {
    switch (sampleCount) {
    case 1:
        return VK_SAMPLE_COUNT_1_BIT;
    case 2:
        return VK_SAMPLE_COUNT_2_BIT;
    case 4:
        return VK_SAMPLE_COUNT_4_BIT;
    case 8:
        return VK_SAMPLE_COUNT_8_BIT;
    default:
        return VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
    }
}

[[nodiscard]] constexpr bool has_mutable_view_format(
    TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::rgba8_unorm:
    case TextureFormat::rgba8_srgb:
    case TextureFormat::bgra8_unorm:
    case TextureFormat::bgra8_srgb:
    case TextureFormat::bc1_rgba_unorm:
    case TextureFormat::bc1_rgba_srgb:
    case TextureFormat::bc3_rgba_unorm:
    case TextureFormat::bc3_rgba_srgb:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr VkImageCreateFlags vulkan_image_flags(
    const TextureDesc& desc) noexcept {
    VkImageCreateFlags flags = 0;
    if (desc.dimension == TextureDimension::cube) {
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    if (has_mutable_view_format(desc.format)) {
        flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }
    return flags;
}

[[nodiscard]] VkImageAspectFlags vulkan_aspect(TextureAspect aspect) {
    VkImageAspectFlags result = 0;
    if (has_aspect(aspect, TextureAspect::color)) {
        result |= VK_IMAGE_ASPECT_COLOR_BIT;
    }
    if (has_aspect(aspect, TextureAspect::depth)) {
        result |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (has_aspect(aspect, TextureAspect::stencil)) {
        result |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return result;
}

[[nodiscard]] constexpr VkShaderStageFlags vulkan_shader_stages(
    ShaderStageMask stages) noexcept {
    VkShaderStageFlags native = 0;
    if (has_stage(stages, ShaderStageMask::vertex)) {
        native |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (has_stage(stages, ShaderStageMask::fragment)) {
        native |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (has_stage(stages, ShaderStageMask::compute)) {
        native |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return native;
}

[[nodiscard]] constexpr VkDescriptorType vulkan_descriptor_type(
    BindingType type) noexcept {
    switch (type) {
    case BindingType::uniform_buffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::storage_buffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::sampled_texture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::storage_texture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

[[nodiscard]] constexpr VkCompareOp vulkan_compare(CompareOp compare) noexcept {
    switch (compare) {
    case CompareOp::never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::less_equal:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::not_equal:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::greater_equal:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

[[nodiscard]] constexpr VkPrimitiveTopology vulkan_topology(
    PrimitiveTopology topology) noexcept {
    switch (topology) {
    case PrimitiveTopology::triangle_list:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::triangle_strip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::line_list:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::point_list:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::patch_list:
        return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

[[nodiscard]] constexpr VkPolygonMode vulkan_polygon_mode(
    PolygonMode mode) noexcept {
    switch (mode) {
    case PolygonMode::fill:
        return VK_POLYGON_MODE_FILL;
    case PolygonMode::line:
        return VK_POLYGON_MODE_LINE;
    case PolygonMode::point:
        return VK_POLYGON_MODE_POINT;
    }
    return VK_POLYGON_MODE_MAX_ENUM;
}

[[nodiscard]] constexpr VkCullModeFlags vulkan_cull_mode(
    CullMode mode) noexcept {
    switch (mode) {
    case CullMode::none:
        return VK_CULL_MODE_NONE;
    case CullMode::front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::back:
        return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

[[nodiscard]] constexpr VkFrontFace vulkan_front_face(
    FrontFace face) noexcept {
    return face == FrontFace::clockwise ? VK_FRONT_FACE_CLOCKWISE
                                        : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

[[nodiscard]] constexpr VkBlendFactor vulkan_blend_factor(
    BlendFactor factor) noexcept {
    switch (factor) {
    case BlendFactor::zero:
        return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::one:
        return VK_BLEND_FACTOR_ONE;
    case BlendFactor::source_color:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::one_minus_source_color:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::destination_color:
        return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::one_minus_destination_color:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::source_alpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::one_minus_source_alpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::destination_alpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::one_minus_destination_alpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_MAX_ENUM;
}

[[nodiscard]] constexpr VkBlendOp vulkan_blend_op(BlendOp operation) noexcept {
    switch (operation) {
    case BlendOp::add:
        return VK_BLEND_OP_ADD;
    case BlendOp::subtract:
        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::reverse_subtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::minimum:
        return VK_BLEND_OP_MIN;
    case BlendOp::maximum:
        return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_MAX_ENUM;
}

[[nodiscard]] constexpr VkFormat vulkan_vertex_format(
    VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::float32:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::float32x2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::float32x3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::float32x4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::uint32:
        return VK_FORMAT_R32_UINT;
    case VertexFormat::uint32x2:
        return VK_FORMAT_R32G32_UINT;
    case VertexFormat::uint32x3:
        return VK_FORMAT_R32G32B32_UINT;
    case VertexFormat::uint32x4:
        return VK_FORMAT_R32G32B32A32_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] constexpr VkSamplerAddressMode vulkan_sampler_address(
    SamplerAddressMode mode) noexcept {
    switch (mode) {
    case SamplerAddressMode::clamp_to_edge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerAddressMode::repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::mirror_repeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

struct VulkanTextureResource {
    VulkanTextureResource(std::shared_ptr<VulkanContext> contextValue,
                          VkImage imageValue, VkDeviceMemory memoryValue,
                          VkDeviceSize allocationSizeValue,
                          TextureDesc descValue, VulkanFormat formatValue,
                          bool hostVisibleValue, bool hostCoherentValue,
                          void* mappedValue, VkImageLayout initialLayout)
        : context(std::move(contextValue)), image(imageValue),
          memory(memoryValue), allocationSize(allocationSizeValue),
          desc(std::move(descValue)), format(formatValue),
          hostVisible(hostVisibleValue), hostCoherent(hostCoherentValue),
          mapped(mappedValue),
          layouts(static_cast<std::size_t>(desc.mipLevels) * desc.arrayLayers,
                  initialLayout) {}

    ~VulkanTextureResource() {
        if (!context || context->device == VK_NULL_HANDLE) {
            return;
        }
        std::lock_guard contextLock{context->mutex};
        std::lock_guard resourceLock{mutex};
        if (mapped != nullptr) {
            context->deviceTable.vkUnmapMemory(context->device, memory);
            mapped = nullptr;
        }
        if (image != VK_NULL_HANDLE) {
            context->deviceTable.vkDestroyImage(context->device, image,
                                                 nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            context->deviceTable.vkFreeMemory(context->device, memory, nullptr);
        }
    }

    [[nodiscard]] std::size_t layout_index(std::uint32_t mip,
                                           std::uint32_t layer) const {
        return static_cast<std::size_t>(layer) * desc.mipLevels + mip;
    }

    std::shared_ptr<VulkanContext> context;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocationSize = 0;
    TextureDesc desc;
    VulkanFormat format;
    bool hostVisible = false;
    bool hostCoherent = false;
    void* mapped = nullptr;
    std::vector<VkImageLayout> layouts;
    std::mutex mutex;
};

struct VulkanTextureViewResource {
    VulkanTextureViewResource(std::shared_ptr<VulkanTextureResource> textureValue,
                              VkImageView viewValue, TextureViewDesc descValue)
        : texture(std::move(textureValue)), view(viewValue),
          desc(std::move(descValue)) {}

    ~VulkanTextureViewResource() {
        if (!texture || !texture->context || view == VK_NULL_HANDLE) {
            return;
        }
        std::lock_guard lock{texture->context->mutex};
        texture->context->deviceTable.vkDestroyImageView(
            texture->context->device, view, nullptr);
    }

    std::shared_ptr<VulkanTextureResource> texture;
    VkImageView view = VK_NULL_HANDLE;
    TextureViewDesc desc;
};

[[nodiscard]] constexpr VkImageViewType vulkan_view_type(
    TextureDimension dimension, std::uint32_t layerCount) noexcept {
    switch (dimension) {
    case TextureDimension::d1:
        return layerCount == 1 ? VK_IMAGE_VIEW_TYPE_1D
                               : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case TextureDimension::d2:
        return layerCount == 1 ? VK_IMAGE_VIEW_TYPE_2D
                               : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::d3:
        return VK_IMAGE_VIEW_TYPE_3D;
    case TextureDimension::cube:
        return layerCount == 6 ? VK_IMAGE_VIEW_TYPE_CUBE
                               : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

struct VulkanProbe {
    std::shared_ptr<VulkanContext> context;
    std::string adapterName;
    std::size_t deviceLocalBudget = 1024u * 1024u * 1024u;
    bool hostCoherent = false;
};

[[nodiscard]] VkBufferUsageFlags vulkan_buffer_usage(BufferUsage usage) {
    VkBufferUsageFlags native = 0;
    if (has_usage(usage, BufferUsage::vertex)) {
        native |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (has_usage(usage, BufferUsage::index)) {
        native |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (has_usage(usage, BufferUsage::uniform)) {
        native |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (has_usage(usage, BufferUsage::storage)) {
        native |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (has_usage(usage, BufferUsage::indirect)) {
        native |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    if (has_usage(usage, BufferUsage::copy_source)) {
        native |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (has_usage(usage, BufferUsage::copy_destination)) {
        native |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return native;
}

[[nodiscard]] Result<std::uint32_t> find_memory_type(
    const VulkanContext& context, std::uint32_t allowedTypes,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) {
    for (std::uint32_t index = 0;
         index < context.memoryProperties.memoryTypeCount; ++index) {
        const auto flags =
            context.memoryProperties.memoryTypes[index].propertyFlags;
        if ((allowedTypes & (1u << index)) != 0 &&
            (flags & required) == required &&
            (flags & preferred) == preferred) {
            return index;
        }
    }
    for (std::uint32_t index = 0;
         index < context.memoryProperties.memoryTypeCount; ++index) {
        const auto flags =
            context.memoryProperties.memoryTypes[index].propertyFlags;
        if ((allowedTypes & (1u << index)) != 0 &&
            (flags & required) == required) {
            return index;
        }
    }
    return Status::failure(StatusCode::unsupported,
                           "Vulkan found no compatible memory type");
}

[[nodiscard]] Result<std::shared_ptr<void>> create_vulkan_buffer(
    const std::shared_ptr<void>& nativeContext, const BufferDesc& desc) {
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    if (!context || context->device == VK_NULL_HANDLE) {
        return Status::failure(StatusCode::device_lost,
                               "the Vulkan native context is unavailable");
    }
    if (desc.memory == MemoryDomain::external || desc.shareable) {
        return Status::failure(
            StatusCode::unsupported,
            "Vulkan external buffer memory is not implemented");
    }

    const auto usage = vulkan_buffer_usage(desc.usage);
    if (usage == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan buffer usage is empty");
    }
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = static_cast<VkDeviceSize>(desc.size),
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkMemoryRequirements requirements{};
    bool hostVisible = false;
    bool hostCoherent = false;
    void* mapped = nullptr;
    std::lock_guard lock{context->mutex};
    auto result = context->deviceTable.vkCreateBuffer(
        context->device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::out_of_memory,
                              "Vulkan buffer creation failed", result);
    }
    context->deviceTable.vkGetBufferMemoryRequirements(context->device, buffer,
                                                        &requirements);

    VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryPropertyFlags preferred = 0;
    if (desc.memory == MemoryDomain::upload ||
        desc.memory == MemoryDomain::readback) {
        required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        preferred = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (desc.memory == MemoryDomain::readback) {
            preferred |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        }
    }
    auto memoryType = find_memory_type(*context, requirements.memoryTypeBits,
                                       required, preferred);
    if (!memoryType.ok()) {
        context->deviceTable.vkDestroyBuffer(context->device, buffer, nullptr);
        return memoryType.status();
    }
    const auto memoryFlags =
        context->memoryProperties.memoryTypes[memoryType.value()].propertyFlags;
    hostVisible = (memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    hostCoherent = (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    const VkMemoryAllocateInfo allocationInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType.value(),
    };
    result = context->deviceTable.vkAllocateMemory(
        context->device, &allocationInfo, nullptr, &memory);
    if (result == VK_SUCCESS) {
        result = context->deviceTable.vkBindBufferMemory(
            context->device, buffer, memory, 0);
    }
    if (result == VK_SUCCESS && desc.mappedAtCreation) {
        result = context->deviceTable.vkMapMemory(
            context->device, memory, 0, requirements.size, 0, &mapped);
    }
    if (result != VK_SUCCESS) {
        if (mapped != nullptr) {
            context->deviceTable.vkUnmapMemory(context->device, memory);
        }
        context->deviceTable.vkDestroyBuffer(context->device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE) {
            context->deviceTable.vkFreeMemory(context->device, memory, nullptr);
        }
        return vulkan_failure(
            result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
                    result == VK_ERROR_OUT_OF_HOST_MEMORY
                ? StatusCode::out_of_memory
                : StatusCode::backend_error,
            "Vulkan buffer memory allocation failed", result);
    }

    try {
        return std::static_pointer_cast<void>(
            std::make_shared<VulkanBufferResource>(
                context, buffer, memory, requirements.size, desc.size,
                hostVisible, hostCoherent, mapped));
    } catch (const std::bad_alloc&) {
        if (mapped != nullptr) {
            context->deviceTable.vkUnmapMemory(context->device, memory);
        }
        context->deviceTable.vkDestroyBuffer(context->device, buffer, nullptr);
        context->deviceTable.vkFreeMemory(context->device, memory, nullptr);
        return Status::failure(StatusCode::out_of_memory,
                               "Vulkan buffer resource allocation failed");
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_vulkan_shader(
    const std::shared_ptr<void>& nativeContext, const ShaderDesc& desc) {
    constexpr std::uint32_t spirvMagic = 0x07230203u;
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    if (!context || context->device == VK_NULL_HANDLE) {
        return Status::failure(StatusCode::device_lost,
                               "the Vulkan native context is unavailable");
    }
    if (desc.format != ShaderByteFormat::spirv) {
        return Status::failure(StatusCode::unsupported,
                               "Vulkan accepts SPIR-V shader variants");
    }
    if (desc.code.empty() || desc.code.size() % sizeof(std::uint32_t) != 0) {
        return Status::failure(
            StatusCode::invalid_argument,
            "Vulkan SPIR-V shader bytecode must contain complete words");
    }

    std::vector<std::uint32_t> words(desc.code.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), desc.code.data(), desc.code.size());
    if (words.front() != spirvMagic) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan shader bytecode has no SPIR-V magic");
    }

    const VkShaderModuleCreateInfo moduleInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = desc.code.size(),
        .pCode = words.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    std::lock_guard lock{context->mutex};
    const auto result = context->deviceTable.vkCreateShaderModule(
        context->device, &moduleInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        const auto code = result == VK_ERROR_OUT_OF_HOST_MEMORY
                              ? StatusCode::out_of_memory
                              : StatusCode::invalid_argument;
        return vulkan_failure(code,
                              "Vulkan shader-module creation failed", result);
    }
    try {
        return std::static_pointer_cast<void>(
            std::make_shared<VulkanShaderResource>(context, module, desc));
    } catch (const std::bad_alloc&) {
        context->deviceTable.vkDestroyShaderModule(context->device, module,
                                                    nullptr);
        return Status::failure(StatusCode::out_of_memory,
                               "Vulkan shader resource allocation failed");
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_vulkan_sampler(
    const std::shared_ptr<void>& nativeContext, const SamplerDesc& desc) {
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    if (!context || context->device == VK_NULL_HANDLE) {
        return Status::failure(StatusCode::device_lost,
                               "the Vulkan native context is unavailable");
    }
    if (desc.maxAnisotropy != 1.0F) {
        return Status::failure(StatusCode::unsupported,
                               "Vulkan anisotropic sampling is not enabled");
    }
    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = desc.magFilter == Filter::linear ? VK_FILTER_LINEAR
                                                       : VK_FILTER_NEAREST,
        .minFilter = desc.minFilter == Filter::linear ? VK_FILTER_LINEAR
                                                       : VK_FILTER_NEAREST,
        .mipmapMode = desc.mipFilter == Filter::linear
                          ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                          : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = vulkan_sampler_address(desc.addressU),
        .addressModeV = vulkan_sampler_address(desc.addressV),
        .addressModeW = vulkan_sampler_address(desc.addressW),
        .mipLodBias = 0.0F,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0F,
        .compareEnable = desc.compare == CompareOp::always ? VK_FALSE : VK_TRUE,
        .compareOp = vulkan_compare(desc.compare),
        .minLod = desc.lodMin,
        .maxLod = desc.lodMax,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    std::lock_guard lock{context->mutex};
    const auto result = context->deviceTable.vkCreateSampler(
        context->device, &samplerInfo, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        return vulkan_failure(
            result == VK_ERROR_OUT_OF_HOST_MEMORY ||
                    result == VK_ERROR_OUT_OF_DEVICE_MEMORY
                ? StatusCode::out_of_memory
                : StatusCode::backend_error,
            "Vulkan sampler creation failed", result);
    }
    try {
        return std::static_pointer_cast<void>(
            std::make_shared<VulkanSamplerResource>(context, sampler));
    } catch (const std::bad_alloc&) {
        context->deviceTable.vkDestroySampler(context->device, sampler, nullptr);
        return Status::failure(StatusCode::out_of_memory,
                               "Vulkan sampler resource allocation failed");
    }
}

struct VulkanPipelineLayoutObjects {
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

void destroy_vulkan_pipeline_layout(VulkanContext& context,
                                    VulkanPipelineLayoutObjects& objects) {
    if (objects.layout != VK_NULL_HANDLE) {
        context.deviceTable.vkDestroyPipelineLayout(context.device,
                                                     objects.layout, nullptr);
        objects.layout = VK_NULL_HANDLE;
    }
    for (const auto setLayout : objects.setLayouts) {
        context.deviceTable.vkDestroyDescriptorSetLayout(
            context.device, setLayout, nullptr);
    }
    objects.setLayouts.clear();
}

[[nodiscard]] Result<VulkanPipelineLayoutObjects>
create_vulkan_pipeline_layout(VulkanContext& context,
                              const detail::NativePipelineLayout& layout,
                              std::span<const ShaderDesc* const> shaders) {
    for (const auto* shader : shaders) {
        if (shader == nullptr) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Vulkan pipeline shader is null");
        }
        for (const auto& mapping : shader->bindingMap) {
            if (mapping.nativeGroup != mapping.group ||
                mapping.nativeBinding != mapping.binding ||
                mapping.nativeArrayElement != mapping.arrayElement) {
                return Status::failure(
                    StatusCode::unsupported,
                    "Vulkan non-identity shader binding remaps are not implemented");
            }
        }
    }

    std::uint32_t setCount = 0;
    for (const auto& group : layout.bindGroups) {
        setCount = std::max(setCount, group.group + 1u);
    }
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> nativeBindings(
        setCount);
    for (const auto& group : layout.bindGroups) {
        auto& bindings = nativeBindings[group.group];
        bindings.reserve(group.entries.size());
        for (const auto& entry : group.entries) {
            const auto descriptorType = vulkan_descriptor_type(entry.type);
            const auto stageFlags = vulkan_shader_stages(entry.visibility);
            if (descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM ||
                stageFlags == 0) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan pipeline layout contains an invalid binding");
            }
            bindings.push_back({
                .binding = entry.binding,
                .descriptorType = descriptorType,
                .descriptorCount = entry.arrayCount,
                .stageFlags = stageFlags,
                .pImmutableSamplers = nullptr,
            });
        }
    }

    VulkanPipelineLayoutObjects objects;
    objects.setLayouts.reserve(setCount);
    for (const auto& bindings : nativeBindings) {
        const VkDescriptorSetLayoutCreateInfo setInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<std::uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        const auto result = context.deviceTable.vkCreateDescriptorSetLayout(
            context.device, &setInfo, nullptr, &setLayout);
        if (result != VK_SUCCESS) {
            destroy_vulkan_pipeline_layout(context, objects);
            return vulkan_failure(StatusCode::backend_error,
                                  "Vulkan descriptor-set layout creation failed",
                                  result);
        }
        objects.setLayouts.push_back(setLayout);
    }

    std::vector<VkPushConstantRange> pushConstants;
    pushConstants.reserve(layout.pushConstants.size());
    for (const auto& range : layout.pushConstants) {
        pushConstants.push_back({
            .stageFlags = vulkan_shader_stages(shader_stage_mask(range.stage)),
            .offset = range.offset,
            .size = range.size,
        });
    }
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount =
            static_cast<std::uint32_t>(objects.setLayouts.size()),
        .pSetLayouts = objects.setLayouts.data(),
        .pushConstantRangeCount =
            static_cast<std::uint32_t>(pushConstants.size()),
        .pPushConstantRanges = pushConstants.data(),
    };
    const auto result = context.deviceTable.vkCreatePipelineLayout(
        context.device, &layoutInfo, nullptr, &objects.layout);
    if (result != VK_SUCCESS) {
        destroy_vulkan_pipeline_layout(context, objects);
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan pipeline-layout creation failed", result);
    }
    return objects;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_vulkan_compute_pipeline(
    const std::shared_ptr<void>& nativeContext,
    const ComputePipelineDesc& desc,
    const detail::NativePipelineLayout& layout,
    const std::shared_ptr<void>& shaderResource) {
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    const auto shader =
        std::static_pointer_cast<VulkanShaderResource>(shaderResource);
    if (!context || context->device == VK_NULL_HANDLE || !shader ||
        shader->module == VK_NULL_HANDLE ||
        shader->desc.stage != ShaderStage::compute) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan compute shader is invalid");
    }

    std::lock_guard lock{context->mutex};
    const std::array<const ShaderDesc*, 1> shaders{&shader->desc};
    auto layoutResult =
        create_vulkan_pipeline_layout(*context, layout, shaders);
    if (!layoutResult.ok()) {
        return layoutResult.status();
    }
    auto nativeLayout = std::move(layoutResult).value();

    std::vector<VkSpecializationMapEntry> entries;
    std::vector<std::uint32_t> values;
    entries.reserve(desc.specializationConstants.size());
    values.reserve(desc.specializationConstants.size());
    for (const auto& value : desc.specializationConstants) {
        entries.push_back({
            .constantID = value.id,
            .offset = static_cast<std::uint32_t>(values.size() *
                                                 sizeof(std::uint32_t)),
            .size = sizeof(std::uint32_t),
        });
        values.push_back(value.valueBits);
    }
    const VkSpecializationInfo specialization{
        .mapEntryCount = static_cast<std::uint32_t>(entries.size()),
        .pMapEntries = entries.data(),
        .dataSize = values.size() * sizeof(std::uint32_t),
        .pData = values.data(),
    };
    const VkPipelineShaderStageCreateInfo stage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shader->module,
        .pName = shader->desc.entryPoint.c_str(),
        .pSpecializationInfo = entries.empty() ? nullptr : &specialization,
    };
    const VkComputePipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = stage,
        .layout = nativeLayout.layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result = context->deviceTable.vkCreateComputePipelines(
        context->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        destroy_vulkan_pipeline_layout(*context, nativeLayout);
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan compute-pipeline creation failed", result);
    }
    try {
        return std::static_pointer_cast<void>(
            std::make_shared<VulkanPipelineResource>(
                context, std::move(nativeLayout.setLayouts),
                nativeLayout.layout, pipeline,
                VK_PIPELINE_BIND_POINT_COMPUTE));
    } catch (const std::bad_alloc&) {
        context->deviceTable.vkDestroyPipeline(context->device, pipeline,
                                                nullptr);
        destroy_vulkan_pipeline_layout(*context, nativeLayout);
        return Status::failure(StatusCode::out_of_memory,
                               "Vulkan compute-pipeline allocation failed");
    }
}

[[nodiscard]] Result<VkRenderPass> create_vulkan_color_render_pass(
    VulkanContext& context, VkFormat format, VkAttachmentLoadOp loadOp,
    VkAttachmentStoreOp storeOp) {
    const VkAttachmentDescription attachment{
        .flags = 0,
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = loadOp,
        .storeOp = storeOp,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference colorReference{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorReference,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };
    const VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    };
    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    VkRenderPass renderPass = VK_NULL_HANDLE;
    const auto result = context.deviceTable.vkCreateRenderPass(
        context.device, &renderPassInfo, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan render-pass creation failed", result);
    }
    return renderPass;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_vulkan_graphics_pipeline(
    const std::shared_ptr<void>& nativeContext, const PipelineDesc& desc,
    const detail::NativePipelineLayout& layout,
    const std::shared_ptr<void>& vertexResource,
    const std::shared_ptr<void>& fragmentResource) {
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    const auto vertex =
        std::static_pointer_cast<VulkanShaderResource>(vertexResource);
    const auto fragment =
        std::static_pointer_cast<VulkanShaderResource>(fragmentResource);
    if (!context || context->device == VK_NULL_HANDLE || !vertex || !fragment ||
        vertex->module == VK_NULL_HANDLE || fragment->module == VK_NULL_HANDLE ||
        vertex->desc.stage != ShaderStage::vertex ||
        fragment->desc.stage != ShaderStage::fragment ||
        desc.colorTargets.size() != 1 ||
        desc.depthStencil.format != TextureFormat::unknown ||
        desc.multisample.sampleCount != 1 ||
        desc.topology == PrimitiveTopology::patch_list) {
        return Status::failure(
            StatusCode::unsupported,
            "this Vulkan graphics-pipeline configuration is unsupported");
    }

    std::lock_guard lock{context->mutex};
    const std::array<const ShaderDesc*, 2> shaders{&vertex->desc,
                                                   &fragment->desc};
    auto layoutResult =
        create_vulkan_pipeline_layout(*context, layout, shaders);
    if (!layoutResult.ok()) {
        return layoutResult.status();
    }
    auto nativeLayout = std::move(layoutResult).value();
    const auto colorFormat = vulkan_format(desc.colorTargets.front().format);
    auto renderPassResult = create_vulkan_color_render_pass(
        *context, colorFormat.format, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_STORE);
    if (!renderPassResult.ok()) {
        destroy_vulkan_pipeline_layout(*context, nativeLayout);
        return renderPassResult.status();
    }
    const auto renderPass = renderPassResult.value();

    std::vector<VkSpecializationMapEntry> specializationEntries;
    std::vector<std::uint32_t> specializationValues;
    specializationEntries.reserve(desc.specializationConstants.size());
    specializationValues.reserve(desc.specializationConstants.size());
    for (const auto& value : desc.specializationConstants) {
        specializationEntries.push_back({
            .constantID = value.id,
            .offset = static_cast<std::uint32_t>(
                specializationValues.size() * sizeof(std::uint32_t)),
            .size = sizeof(std::uint32_t),
        });
        specializationValues.push_back(value.valueBits);
    }
    const VkSpecializationInfo specialization{
        .mapEntryCount =
            static_cast<std::uint32_t>(specializationEntries.size()),
        .pMapEntries = specializationEntries.data(),
        .dataSize = specializationValues.size() * sizeof(std::uint32_t),
        .pData = specializationValues.data(),
    };
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex->module,
            .pName = vertex->desc.entryPoint.c_str(),
            .pSpecializationInfo = specializationEntries.empty()
                                       ? nullptr
                                       : &specialization,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment->module,
            .pName = fragment->desc.entryPoint.c_str(),
            .pSpecializationInfo = specializationEntries.empty()
                                       ? nullptr
                                       : &specialization,
        },
    }};

    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    for (std::uint32_t binding = 0; binding < desc.vertexBuffers.size();
         ++binding) {
        const auto& source = desc.vertexBuffers[binding];
        vertexBindings.push_back({
            .binding = binding,
            .stride = source.stride,
            .inputRate = source.stepMode == VertexStepMode::instance
                             ? VK_VERTEX_INPUT_RATE_INSTANCE
                             : VK_VERTEX_INPUT_RATE_VERTEX,
        });
        for (const auto& attribute : source.attributes) {
            vertexAttributes.push_back({
                .location = attribute.location,
                .binding = binding,
                .format = vulkan_vertex_format(attribute.format),
                .offset = attribute.offset,
            });
        }
    }
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount =
            static_cast<std::uint32_t>(vertexBindings.size()),
        .pVertexBindingDescriptions = vertexBindings.data(),
        .vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = vulkan_topology(desc.topology),
        .primitiveRestartEnable = VK_FALSE,
    };

    const bool dynamicViewport =
        desc.viewports.empty() ||
        has_dynamic_state(desc.dynamicState, DynamicState::viewport);
    const bool dynamicScissor =
        desc.scissors.empty() ||
        has_dynamic_state(desc.dynamicState, DynamicState::scissor);
    std::vector<VkViewport> viewports;
    for (const auto& viewport : desc.viewports) {
        viewports.push_back({viewport.x, viewport.y, viewport.width,
                             viewport.height, viewport.minimumDepth,
                             viewport.maximumDepth});
    }
    if (viewports.empty()) {
        viewports.push_back({0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F});
    }
    std::vector<VkRect2D> scissors;
    for (const auto& scissor : desc.scissors) {
        scissors.push_back({
            {scissor.x, scissor.y}, {scissor.width, scissor.height}});
    }
    if (scissors.empty()) {
        scissors.push_back({{0, 0}, {1, 1}});
    }
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = static_cast<std::uint32_t>(viewports.size()),
        .pViewports = viewports.data(),
        .scissorCount = static_cast<std::uint32_t>(scissors.size()),
        .pScissors = scissors.data(),
    };
    std::vector<VkDynamicState> dynamicStates;
    if (dynamicViewport) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    }
    if (dynamicScissor) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
    }
    if (has_dynamic_state(desc.dynamicState, DynamicState::blend_constant)) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
    }
    if (has_dynamic_state(desc.dynamicState, DynamicState::stencil_reference)) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
    }
    if (has_dynamic_state(desc.dynamicState, DynamicState::depth_bias)) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable =
            desc.rasterization.depthClampEnabled ? VK_TRUE : VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = vulkan_polygon_mode(desc.rasterization.polygonMode),
        .cullMode = vulkan_cull_mode(desc.rasterization.cullMode),
        .frontFace = vulkan_front_face(desc.rasterization.frontFace),
        .depthBiasEnable =
            (desc.rasterization.depthBias != 0.0F ||
             desc.rasterization.depthBiasSlopeScale != 0.0F ||
             desc.rasterization.depthBiasClamp != 0.0F ||
             has_dynamic_state(desc.dynamicState, DynamicState::depth_bias))
                ? VK_TRUE
                : VK_FALSE,
        .depthBiasConstantFactor = desc.rasterization.depthBias,
        .depthBiasClamp = desc.rasterization.depthBiasClamp,
        .depthBiasSlopeFactor = desc.rasterization.depthBiasSlopeScale,
        .lineWidth = 1.0F,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0F,
        .pSampleMask = &desc.multisample.sampleMask,
        .alphaToCoverageEnable =
            desc.multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    const auto& target = desc.colorTargets.front();
    const VkPipelineColorBlendAttachmentState colorBlend{
        .blendEnable = target.blend.enabled ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = vulkan_blend_factor(
            target.blend.color.sourceFactor),
        .dstColorBlendFactor = vulkan_blend_factor(
            target.blend.color.destinationFactor),
        .colorBlendOp = vulkan_blend_op(target.blend.color.operation),
        .srcAlphaBlendFactor = vulkan_blend_factor(
            target.blend.alpha.sourceFactor),
        .dstAlphaBlendFactor = vulkan_blend_factor(
            target.blend.alpha.destinationFactor),
        .alphaBlendOp = vulkan_blend_op(target.blend.alpha.operation),
        .colorWriteMask = target.writeMask,
    };
    const VkPipelineColorBlendStateCreateInfo colorBlendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlend,
        .blendConstants = {desc.blendConstant[0], desc.blendConstant[1],
                           desc.blendConstant[2], desc.blendConstant[3]},
    };
    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = dynamicStates.empty() ? nullptr : &dynamicState,
        .layout = nativeLayout.layout,
        .renderPass = renderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result = context->deviceTable.vkCreateGraphicsPipelines(
        context->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        context->deviceTable.vkDestroyRenderPass(context->device, renderPass,
                                                 nullptr);
        destroy_vulkan_pipeline_layout(*context, nativeLayout);
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan graphics-pipeline creation failed",
                              result);
    }
    try {
        return std::static_pointer_cast<void>(
            std::make_shared<VulkanPipelineResource>(
                context, std::move(nativeLayout.setLayouts),
                nativeLayout.layout, pipeline,
                VK_PIPELINE_BIND_POINT_GRAPHICS, renderPass, dynamicViewport,
                dynamicScissor));
    } catch (const std::bad_alloc&) {
        context->deviceTable.vkDestroyPipeline(context->device, pipeline,
                                                nullptr);
        context->deviceTable.vkDestroyRenderPass(context->device, renderPass,
                                                 nullptr);
        destroy_vulkan_pipeline_layout(*context, nativeLayout);
        return Status::failure(StatusCode::out_of_memory,
                               "Vulkan graphics-pipeline allocation failed");
    }
}

[[nodiscard]] VkImageUsageFlags vulkan_texture_usage(TextureUsage usage) {
    VkImageUsageFlags native = 0;
    if (has_usage(usage, TextureUsage::sampled)) {
        native |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (has_usage(usage, TextureUsage::storage)) {
        native |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (has_usage(usage, TextureUsage::color_attachment)) {
        native |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (has_usage(usage, TextureUsage::depth_stencil_attachment)) {
        native |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (has_usage(usage, TextureUsage::copy_source)) {
        native |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (has_usage(usage, TextureUsage::copy_destination)) {
        native |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return native;
}

[[nodiscard]] VkFormatFeatureFlags required_format_features(
    TextureUsage usage) {
    VkFormatFeatureFlags features = 0;
    if (has_usage(usage, TextureUsage::sampled)) {
        features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }
    if (has_usage(usage, TextureUsage::storage)) {
        features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    }
    if (has_usage(usage, TextureUsage::color_attachment)) {
        features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    }
    if (has_usage(usage, TextureUsage::depth_stencil_attachment)) {
        features |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (has_usage(usage, TextureUsage::copy_source)) {
        features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
    if (has_usage(usage, TextureUsage::copy_destination)) {
        features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    }
    return features;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_vulkan_texture(
    const std::shared_ptr<void>& nativeContext, const TextureDesc& desc) {
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    if (!context || context->device == VK_NULL_HANDLE) {
        return Status::failure(StatusCode::device_lost,
                               "the Vulkan native context is unavailable");
    }
    const auto format = vulkan_format(desc.format);
    const auto imageType = vulkan_image_type(desc.dimension);
    const auto sampleCount = vulkan_sample_count(desc.sampleCount);
    const auto imageFlags = vulkan_image_flags(desc);
    const auto tiling = desc.memory == MemoryDomain::device_local
                            ? VK_IMAGE_TILING_OPTIMAL
                            : VK_IMAGE_TILING_LINEAR;
    if (desc.memory == MemoryDomain::external || desc.shareable ||
        format.format == VK_FORMAT_UNDEFINED ||
        imageType == VK_IMAGE_TYPE_MAX_ENUM ||
        sampleCount == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM ||
        (tiling == VK_IMAGE_TILING_LINEAR && desc.sampleCount != 1) ||
        has_usage(desc.usage, TextureUsage::present)) {
        return Status::failure(
            StatusCode::unsupported,
            "this Vulkan texture shape, format, sample count, or memory mode "
            "is unsupported");
    }
    auto usage = vulkan_texture_usage(desc.usage);
    if (usage == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan texture usage is empty");
    }
    constexpr auto viewCompatibleUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if ((usage & viewCompatibleUsage) == 0) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    VkFormatProperties properties{};
    context->instanceTable.vkGetPhysicalDeviceFormatProperties(
        context->physicalDevice, format.format, &properties);
    auto requiredFeatures = required_format_features(desc.usage);
    if (!has_usage(desc.usage, TextureUsage::sampled) &&
        (vulkan_texture_usage(desc.usage) & viewCompatibleUsage) == 0) {
        requiredFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }
    const auto availableFeatures = tiling == VK_IMAGE_TILING_OPTIMAL
                                       ? properties.optimalTilingFeatures
                                       : properties.linearTilingFeatures;
    if ((availableFeatures & requiredFeatures) !=
        requiredFeatures) {
        return Status::failure(
            StatusCode::unsupported,
            "the Vulkan adapter does not support the requested texture format "
            "usage");
    }
    VkImageFormatProperties imageProperties{};
    const auto propertiesResult =
        context->instanceTable.vkGetPhysicalDeviceImageFormatProperties(
            context->physicalDevice, format.format, imageType,
            tiling, usage, imageFlags, &imageProperties);
    if (propertiesResult != VK_SUCCESS ||
        desc.extent.width > imageProperties.maxExtent.width ||
        desc.extent.height > imageProperties.maxExtent.height ||
        desc.extent.depth > imageProperties.maxExtent.depth ||
        desc.mipLevels > imageProperties.maxMipLevels ||
        desc.arrayLayers > imageProperties.maxArrayLayers ||
        (imageProperties.sampleCounts & sampleCount) == 0) {
        return Status::failure(
            StatusCode::unsupported,
            "the Vulkan adapter does not support the requested texture shape");
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = imageFlags,
        .imageType = imageType,
        .format = format.format,
        .extent = {desc.extent.width, desc.extent.height, desc.extent.depth},
        .mipLevels = desc.mipLevels,
        .arrayLayers = desc.arrayLayers,
        .samples = sampleCount,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = tiling == VK_IMAGE_TILING_LINEAR
                             ? VK_IMAGE_LAYOUT_PREINITIALIZED
                             : VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkMemoryRequirements requirements{};
    bool hostVisible = false;
    bool hostCoherent = false;
    void* mapped = nullptr;
    std::lock_guard lock{context->mutex};
    auto result = context->deviceTable.vkCreateImage(
        context->device, &imageInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::out_of_memory,
                              "Vulkan texture creation failed", result);
    }
    context->deviceTable.vkGetImageMemoryRequirements(context->device, image,
                                                       &requirements);
    VkMemoryPropertyFlags requiredMemory =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryPropertyFlags preferredMemory = 0;
    if (tiling == VK_IMAGE_TILING_LINEAR) {
        requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (desc.memory == MemoryDomain::readback) {
            preferredMemory |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        }
    }
    auto memoryType = find_memory_type(*context, requirements.memoryTypeBits,
                                       requiredMemory, preferredMemory);
    if (!memoryType.ok()) {
        context->deviceTable.vkDestroyImage(context->device, image, nullptr);
        return memoryType.status();
    }
    const VkMemoryAllocateInfo allocationInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType.value(),
    };
    result = context->deviceTable.vkAllocateMemory(
        context->device, &allocationInfo, nullptr, &memory);
    if (result == VK_SUCCESS) {
        result = context->deviceTable.vkBindImageMemory(context->device, image,
                                                        memory, 0);
    }
    if (result == VK_SUCCESS && tiling == VK_IMAGE_TILING_LINEAR) {
        result = context->deviceTable.vkMapMemory(
            context->device, memory, 0, requirements.size, 0, &mapped);
        const auto memoryFlags = context->memoryProperties
                                     .memoryTypes[memoryType.value()]
                                     .propertyFlags;
        hostVisible =
            (memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        hostCoherent =
            (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
    if (result != VK_SUCCESS) {
        if (mapped != nullptr) {
            context->deviceTable.vkUnmapMemory(context->device, memory);
        }
        context->deviceTable.vkDestroyImage(context->device, image, nullptr);
        if (memory != VK_NULL_HANDLE) {
            context->deviceTable.vkFreeMemory(context->device, memory, nullptr);
        }
        return vulkan_failure(
            result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
                    result == VK_ERROR_OUT_OF_HOST_MEMORY
                ? StatusCode::out_of_memory
                : StatusCode::backend_error,
            "Vulkan texture memory allocation failed", result);
    }
    try {
        return std::static_pointer_cast<void>(
            std::make_shared<VulkanTextureResource>(
                context, image, memory, requirements.size, desc, format,
                hostVisible, hostCoherent, mapped,
                tiling == VK_IMAGE_TILING_LINEAR
                    ? VK_IMAGE_LAYOUT_PREINITIALIZED
                    : VK_IMAGE_LAYOUT_UNDEFINED));
    } catch (const std::bad_alloc&) {
        if (mapped != nullptr) {
            context->deviceTable.vkUnmapMemory(context->device, memory);
        }
        context->deviceTable.vkDestroyImage(context->device, image, nullptr);
        context->deviceTable.vkFreeMemory(context->device, memory, nullptr);
        return Status::failure(StatusCode::out_of_memory,
                               "Vulkan texture resource allocation failed");
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_vulkan_texture_view(
    const std::shared_ptr<void>& nativeResource,
    const TextureViewDesc& desc) {
    const auto texture =
        std::static_pointer_cast<VulkanTextureResource>(nativeResource);
    const auto format = vulkan_format(desc.format);
    const auto viewType =
        vulkan_view_type(desc.dimension, desc.range.arrayLayerCount);
    if (!texture || texture->image == VK_NULL_HANDLE ||
        format.format == VK_FORMAT_UNDEFINED ||
        viewType == VK_IMAGE_VIEW_TYPE_MAX_ENUM ||
        format.aspects != texture->format.aspects ||
        (desc.dimension == TextureDimension::d3 &&
         desc.range.arrayLayerCount != 1) ||
        (desc.dimension == TextureDimension::cube &&
         (desc.range.baseArrayLayer % 6 != 0 ||
          desc.range.arrayLayerCount % 6 != 0))) {
        return Status::failure(StatusCode::unsupported,
                               "this Vulkan texture view is unsupported");
    }
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = texture->image,
        .viewType = viewType,
        .format = format.format,
        .components = {},
        .subresourceRange = {
            .aspectMask = vulkan_aspect(desc.range.aspects),
            .baseMipLevel = desc.range.baseMipLevel,
            .levelCount = desc.range.mipLevelCount,
            .baseArrayLayer = desc.range.baseArrayLayer,
            .layerCount = desc.range.arrayLayerCount,
        },
    };
    VkImageView view = VK_NULL_HANDLE;
    std::lock_guard lock{texture->context->mutex};
    const auto result = texture->context->deviceTable.vkCreateImageView(
        texture->context->device, &viewInfo, nullptr, &view);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan texture-view creation failed", result);
    }
    try {
        return std::static_pointer_cast<void>(
            std::make_shared<VulkanTextureViewResource>(texture, view, desc));
    } catch (const std::bad_alloc&) {
        texture->context->deviceTable.vkDestroyImageView(
            texture->context->device, view, nullptr);
        return Status::failure(StatusCode::out_of_memory,
                               "Vulkan texture-view allocation failed");
    }
}

[[nodiscard]] Status flush_vulkan_memory(VulkanBufferResource& resource) {
    if (resource.hostCoherent) {
        return Status::success();
    }
    const VkMappedMemoryRange range{
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = resource.memory,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    const auto result = resource.context->deviceTable.vkFlushMappedMemoryRanges(
        resource.context->device, 1, &range);
    return result == VK_SUCCESS
               ? Status::success()
               : vulkan_failure(StatusCode::backend_error,
                                "Vulkan buffer flush failed", result);
}

[[nodiscard]] Status invalidate_vulkan_memory(VulkanBufferResource& resource) {
    if (resource.hostCoherent) {
        return Status::success();
    }
    const VkMappedMemoryRange range{
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = resource.memory,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    const auto result =
        resource.context->deviceTable.vkInvalidateMappedMemoryRanges(
            resource.context->device, 1, &range);
    return result == VK_SUCCESS
               ? Status::success()
               : vulkan_failure(StatusCode::backend_error,
                                "Vulkan buffer invalidation failed", result);
}

[[nodiscard]] Status ensure_vulkan_buffer_mapped(
    VulkanBufferResource& resource) {
    if (!resource.hostVisible) {
        return Status::failure(StatusCode::unsupported,
                               "device-local Vulkan buffers are not host "
                               "mappable");
    }
    if (resource.mapped != nullptr) {
        return Status::success();
    }
    const auto result = resource.context->deviceTable.vkMapMemory(
        resource.context->device, resource.memory, 0, resource.allocationSize,
        0, &resource.mapped);
    return result == VK_SUCCESS
               ? Status::success()
               : vulkan_failure(StatusCode::backend_error,
                                "Vulkan buffer mapping failed", result);
}

[[nodiscard]] Result<std::span<std::byte>> map_vulkan_buffer(
    const std::shared_ptr<void>& nativeResource) {
    const auto resource =
        std::static_pointer_cast<VulkanBufferResource>(nativeResource);
    if (!resource) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan buffer resource is invalid");
    }
    std::lock_guard lock{resource->mutex};
    if (auto status = ensure_vulkan_buffer_mapped(*resource); !status.ok()) {
        return status;
    }
    return std::span<std::byte>{static_cast<std::byte*>(resource->mapped),
                                resource->logicalSize};
}

[[nodiscard]] Status unmap_vulkan_buffer(
    const std::shared_ptr<void>& nativeResource) {
    const auto resource =
        std::static_pointer_cast<VulkanBufferResource>(nativeResource);
    if (!resource) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan buffer resource is invalid");
    }
    std::lock_guard lock{resource->mutex};
    if (resource->mapped == nullptr) {
        return Status::failure(StatusCode::invalid_state,
                               "Vulkan buffer is not mapped");
    }
    resource->context->deviceTable.vkUnmapMemory(resource->context->device,
                                                  resource->memory);
    resource->mapped = nullptr;
    return Status::success();
}

[[nodiscard]] Status flush_vulkan_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::size_t size) {
    const auto resource =
        std::static_pointer_cast<VulkanBufferResource>(nativeResource);
    if (!resource || offset > resource->logicalSize ||
        size > resource->logicalSize - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan buffer flush range is invalid");
    }
    std::lock_guard lock{resource->mutex};
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_vulkan_buffer_mapped(*resource); !status.ok()) {
        return status;
    }
    auto status = flush_vulkan_memory(*resource);
    if (temporary) {
        resource->context->deviceTable.vkUnmapMemory(resource->context->device,
                                                      resource->memory);
        resource->mapped = nullptr;
    }
    return status;
}

[[nodiscard]] Status invalidate_vulkan_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::size_t size) {
    const auto resource =
        std::static_pointer_cast<VulkanBufferResource>(nativeResource);
    if (!resource || offset > resource->logicalSize ||
        size > resource->logicalSize - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan buffer invalidate range is invalid");
    }
    std::lock_guard lock{resource->mutex};
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_vulkan_buffer_mapped(*resource); !status.ok()) {
        return status;
    }
    auto status = invalidate_vulkan_memory(*resource);
    if (temporary) {
        resource->context->deviceTable.vkUnmapMemory(resource->context->device,
                                                      resource->memory);
        resource->mapped = nullptr;
    }
    return status;
}

[[nodiscard]] Status write_vulkan_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::span<const std::byte> data) {
    const auto resource =
        std::static_pointer_cast<VulkanBufferResource>(nativeResource);
    if (!resource || offset > resource->logicalSize ||
        data.size() > resource->logicalSize - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan buffer write exceeds allocation");
    }
    std::lock_guard lock{resource->mutex};
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_vulkan_buffer_mapped(*resource); !status.ok()) {
        return status;
    }
    std::memcpy(static_cast<std::byte*>(resource->mapped) + offset, data.data(),
                data.size());
    auto status = flush_vulkan_memory(*resource);
    if (temporary) {
        resource->context->deviceTable.vkUnmapMemory(resource->context->device,
                                                      resource->memory);
        resource->mapped = nullptr;
    }
    return status;
}

[[nodiscard]] Status read_vulkan_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::span<std::byte> data) {
    const auto resource =
        std::static_pointer_cast<VulkanBufferResource>(nativeResource);
    if (!resource || offset > resource->logicalSize ||
        data.size() > resource->logicalSize - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan buffer read exceeds allocation");
    }
    std::lock_guard lock{resource->mutex};
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_vulkan_buffer_mapped(*resource); !status.ok()) {
        return status;
    }
    auto status = invalidate_vulkan_memory(*resource);
    if (status.ok()) {
        std::memcpy(data.data(),
                    static_cast<const std::byte*>(resource->mapped) + offset,
                    data.size());
    }
    if (temporary) {
        resource->context->deviceTable.vkUnmapMemory(resource->context->device,
                                                      resource->memory);
        resource->mapped = nullptr;
    }
    return status;
}

[[nodiscard]] bool vulkan_buffer_texture_region_valid(
    const VulkanBufferResource& buffer, const VulkanTextureResource& texture,
    const BufferTextureCopyRegion& region) {
    const auto aspect = vulkan_aspect(region.texture.subresource.aspect);
    if (aspect == 0 || (aspect & (aspect - 1u)) != 0 ||
        (texture.format.aspects & aspect) != aspect) {
        return false;
    }
    const auto blocksWide = divide_round_up(region.texture.extent.width,
                                            texture.format.blockWidth);
    const auto blocksHigh = divide_round_up(region.texture.extent.height,
                                            texture.format.blockHeight);
    const auto tightRow = static_cast<std::size_t>(blocksWide) *
                          texture.format.bytesPerBlock;
    const auto rowBytes = region.layout.bytesPerRow == 0
                              ? tightRow
                              : region.layout.bytesPerRow;
    const auto rows = region.layout.rowsPerImage == 0
                          ? blocksHigh
                          : region.layout.rowsPerImage;
    if (rowBytes < tightRow || rows < blocksHigh ||
        rowBytes % texture.format.bytesPerBlock != 0) {
        return false;
    }
    const auto offset = region.bufferOffset + region.layout.offset;
    const auto offsetAlignment = std::max(4u, texture.format.bytesPerBlock);
    if (offset < region.bufferOffset || offset % offsetAlignment != 0 ||
        offset > buffer.logicalSize) {
        return false;
    }
    const auto depth = static_cast<std::size_t>(region.texture.extent.depth);
    const auto precedingImages = depth - 1u;
    const auto imageBytes = rowBytes * rows;
    const auto precedingRows = static_cast<std::size_t>(blocksHigh) - 1u;
    const auto available = buffer.logicalSize - offset;
    if (precedingImages != 0 && imageBytes > available / precedingImages) {
        return false;
    }
    const auto imageOffset = precedingImages * imageBytes;
    return precedingRows <= (available - imageOffset) / rowBytes &&
           tightRow <= available - imageOffset - precedingRows * rowBytes;
}

[[nodiscard]] bool vulkan_single_aspect(TextureAspect aspect) noexcept {
    const auto native = vulkan_aspect(aspect);
    return native != 0 && (native & (native - 1u)) == 0;
}

[[nodiscard]] bool vulkan_whole_subresource(
    const VulkanTextureResource& texture, const TextureRegion& region) noexcept {
    const auto mip = region.subresource.mipLevel;
    return region.origin.x == 0 && region.origin.y == 0 &&
           region.origin.z == 0 &&
           region.extent.width == mip_dimension(texture.desc.extent.width, mip) &&
           region.extent.height ==
               mip_dimension(texture.desc.extent.height, mip) &&
           region.extent.depth ==
               (texture.desc.dimension == TextureDimension::d3
                    ? mip_dimension(texture.desc.extent.depth, mip)
                    : 1u);
}

[[nodiscard]] Status validate_vulkan_commands(
    VulkanContext& context, std::span<const detail::NativeCommand> commands) {
    for (const auto& command : commands) {
        if (command.kind == detail::NativeCommandKind::barrier) {
            continue;
        }
        if (command.kind != detail::NativeCommandKind::transfer) {
            switch (command.kind) {
            case detail::NativeCommandKind::begin_render:
            case detail::NativeCommandKind::end_render:
            case detail::NativeCommandKind::begin_compute:
            case detail::NativeCommandKind::end_compute:
            case detail::NativeCommandKind::push_constants:
            case detail::NativeCommandKind::set_viewports:
            case detail::NativeCommandKind::set_scissors:
            case detail::NativeCommandKind::set_blend_constant:
            case detail::NativeCommandKind::set_stencil_reference:
            case detail::NativeCommandKind::set_depth_bias:
            case detail::NativeCommandKind::draw:
            case detail::NativeCommandKind::draw_indexed:
            case detail::NativeCommandKind::dispatch:
                continue;
            case detail::NativeCommandKind::bind_graphics_pipeline:
            case detail::NativeCommandKind::bind_compute_pipeline: {
                const auto pipeline =
                    std::static_pointer_cast<VulkanPipelineResource>(
                        command.object);
                if (!pipeline || pipeline->pipeline == VK_NULL_HANDLE ||
                    pipeline->layout == VK_NULL_HANDLE ||
                    (command.kind ==
                             detail::NativeCommandKind::bind_graphics_pipeline &&
                     pipeline->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) ||
                    (command.kind ==
                             detail::NativeCommandKind::bind_compute_pipeline &&
                     pipeline->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE)) {
                    return Status::failure(
                        StatusCode::invalid_argument,
                        "Vulkan pipeline command is invalid");
                }
                continue;
            }
            case detail::NativeCommandKind::bind_vertex_buffer:
            case detail::NativeCommandKind::bind_index_buffer: {
                const auto buffer =
                    std::static_pointer_cast<VulkanBufferResource>(
                        command.object);
                if (!buffer || buffer->buffer == VK_NULL_HANDLE) {
                    return Status::failure(StatusCode::invalid_argument,
                                           "Vulkan bound buffer is invalid");
                }
                continue;
            }
            case detail::NativeCommandKind::bind_group:
                continue;
            default:
                return Status::failure(
                    StatusCode::unsupported,
                    "this Vulkan command kind is not implemented");
            }
        }
        const auto& transfer = command.transfer;
        switch (transfer.kind) {
        case detail::NativeTransferKind::copy_buffer: {
            const auto source =
                std::static_pointer_cast<VulkanBufferResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanBufferResource>(
                    transfer.destination);
            if (!source || !destination || source->buffer == VK_NULL_HANDLE ||
                destination->buffer == VK_NULL_HANDLE ||
                (transfer.buffer.sourceOffset & 3u) != 0 ||
                (transfer.buffer.destinationOffset & 3u) != 0 ||
                (transfer.buffer.size & 3u) != 0) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan buffer copy resources and ranges must be valid and "
                    "four-byte aligned");
            }
            break;
        }
        case detail::NativeTransferKind::fill_buffer: {
            const auto destination =
                std::static_pointer_cast<VulkanBufferResource>(
                    transfer.destination);
            if (!destination || destination->buffer == VK_NULL_HANDLE ||
                (transfer.buffer.destinationOffset & 3u) != 0 ||
                (transfer.buffer.size & 3u) != 0) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan buffer fill resource and range must be valid and "
                    "four-byte aligned");
            }
            break;
        }
        case detail::NativeTransferKind::copy_buffer_to_texture: {
            const auto source =
                std::static_pointer_cast<VulkanBufferResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            if (!source || !destination ||
                !vulkan_buffer_texture_region_valid(
                    *source, *destination, transfer.bufferTexture)) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan buffer-to-texture resources or layout are invalid");
            }
            break;
        }
        case detail::NativeTransferKind::copy_texture_to_buffer: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanBufferResource>(
                    transfer.destination);
            if (!source || !destination ||
                !vulkan_buffer_texture_region_valid(
                    *destination, *source, transfer.bufferTexture)) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan texture-to-buffer resources or layout are invalid");
            }
            break;
        }
        case detail::NativeTransferKind::copy_texture: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            if (!source || !destination || source == destination ||
                source->format.format != destination->format.format ||
                source->desc.sampleCount != destination->desc.sampleCount ||
                transfer.texture.destination.subresource.aspect !=
                    transfer.texture.source.subresource.aspect ||
                !vulkan_single_aspect(
                    transfer.texture.source.subresource.aspect)) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan texture-copy resources or formats are invalid");
            }
            break;
        }
        case detail::NativeTransferKind::clear_texture: {
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            if (!destination || destination->format.compressed ||
                !vulkan_whole_subresource(*destination,
                                          transfer.texture.destination)) {
                return Status::failure(
                    StatusCode::unsupported,
                    "Vulkan clear requires a complete uncompressed subresource");
            }
            break;
        }
        case detail::NativeTransferKind::resolve_texture: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            if (!source || !destination || source == destination ||
                source->format.format != destination->format.format ||
                source->format.aspects != VK_IMAGE_ASPECT_COLOR_BIT ||
                source->desc.sampleCount <= 1 ||
                destination->desc.sampleCount != 1 ||
                transfer.texture.source.subresource.aspect !=
                    TextureAspect::color ||
                transfer.texture.destination.subresource.aspect !=
                    TextureAspect::color) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Vulkan resolve resources are invalid");
            }
            break;
        }
        case detail::NativeTransferKind::blit_texture: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            if (!source || !destination || source == destination ||
                source->format.format != destination->format.format ||
                source->format.aspects != VK_IMAGE_ASPECT_COLOR_BIT ||
                source->format.compressed || source->desc.sampleCount != 1 ||
                destination->desc.sampleCount != 1 ||
                !vulkan_single_aspect(
                    transfer.blit.source.subresource.aspect) ||
                !vulkan_single_aspect(
                    transfer.blit.destination.subresource.aspect)) {
                return Status::failure(StatusCode::unsupported,
                                       "this Vulkan blit is unsupported");
            }
            VkFormatProperties properties{};
            context.instanceTable.vkGetPhysicalDeviceFormatProperties(
                context.physicalDevice, source->format.format, &properties);
            VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                            VK_FORMAT_FEATURE_BLIT_DST_BIT;
            if (transfer.blit.filter == Filter::linear) {
                required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            }
            if ((properties.optimalTilingFeatures & required) != required) {
                return Status::failure(
                    StatusCode::unsupported,
                    "the Vulkan format does not support the requested blit");
            }
            break;
        }
        }
    }
    return Status::success();
}

[[nodiscard]] VkAccessFlags vulkan_layout_access(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        return VK_ACCESS_HOST_WRITE_BIT;
    case VK_IMAGE_LAYOUT_GENERAL:
        return VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    default:
        return 0;
    }
}

[[nodiscard]] VkPipelineStageFlags vulkan_layout_stage(
    VkImageLayout layout) noexcept {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
    case VK_IMAGE_LAYOUT_GENERAL:
        return VK_PIPELINE_STAGE_HOST_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
               VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    default:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
}

void transition_vulkan_texture(VulkanContext& context,
                               VkCommandBuffer commandBuffer,
                               VulkanTextureResource& texture,
                               const TextureSubresource& subresource,
                               VkImageLayout newLayout) {
    std::lock_guard lock{texture.mutex};
    auto& oldLayout =
        texture.layouts[texture.layout_index(subresource.mipLevel,
                                             subresource.arrayLayer)];
    if (oldLayout == newLayout) {
        return;
    }
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = vulkan_layout_access(oldLayout),
        .dstAccessMask = vulkan_layout_access(newLayout),
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture.image,
        .subresourceRange = {
            .aspectMask = vulkan_aspect(subresource.aspect),
            .baseMipLevel = subresource.mipLevel,
            .levelCount = 1,
            .baseArrayLayer = subresource.arrayLayer,
            .layerCount = 1,
        },
    };
    const auto sourceStage = vulkan_layout_stage(oldLayout);
    context.deviceTable.vkCmdPipelineBarrier(
        commandBuffer, sourceStage, vulkan_layout_stage(newLayout), 0, 0,
        nullptr, 0, nullptr, 1, &barrier);
    oldLayout = newLayout;
}

struct VulkanSubmissionResources {
    std::vector<VkImageView> attachmentViews;
    std::vector<VkRenderPass> renderPasses;
    std::vector<VkFramebuffer> framebuffers;
};

void destroy_vulkan_submission_resources(
    VulkanContext& context, VulkanSubmissionResources& resources) {
    for (const auto framebuffer : resources.framebuffers) {
        context.deviceTable.vkDestroyFramebuffer(context.device, framebuffer,
                                                 nullptr);
    }
    for (const auto renderPass : resources.renderPasses) {
        context.deviceTable.vkDestroyRenderPass(context.device, renderPass,
                                                nullptr);
    }
    for (const auto view : resources.attachmentViews) {
        context.deviceTable.vkDestroyImageView(context.device, view, nullptr);
    }
    resources = {};
}

[[nodiscard]] Status begin_vulkan_render_pass(
    VulkanContext& context, VkCommandBuffer commandBuffer,
    const detail::NativeCommand& command,
    VulkanSubmissionResources& resources) {
    if (command.colorAttachments.size() != 1 ||
        command.colorAttachments.front().resolveTexture ||
        command.depthStencilAttachment.texture) {
        return Status::failure(
            StatusCode::unsupported,
            "this Vulkan render-pass attachment configuration is unsupported");
    }
    const auto texture = std::static_pointer_cast<VulkanTextureResource>(
        command.colorAttachments.front().texture);
    if (!texture || texture->image == VK_NULL_HANDLE ||
        texture->desc.sampleCount != 1 ||
        texture->format.aspects != VK_IMAGE_ASPECT_COLOR_BIT) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan color attachment is invalid");
    }
    transition_vulkan_texture(context, commandBuffer, *texture,
                              {.aspect = TextureAspect::color},
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = texture->format.format,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkImageView view = VK_NULL_HANDLE;
    auto result = context.deviceTable.vkCreateImageView(
        context.device, &viewInfo, nullptr, &view);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan attachment-view creation failed", result);
    }
    const auto& attachment = command.colorAttachments.front();
    auto renderPassResult = create_vulkan_color_render_pass(
        context, texture->format.format,
        attachment.loadOp == LoadOp::clear
            ? VK_ATTACHMENT_LOAD_OP_CLEAR
            : attachment.loadOp == LoadOp::load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        attachment.storeOp == StoreOp::store
            ? VK_ATTACHMENT_STORE_OP_STORE
            : VK_ATTACHMENT_STORE_OP_DONT_CARE);
    if (!renderPassResult.ok()) {
        context.deviceTable.vkDestroyImageView(context.device, view, nullptr);
        return renderPassResult.status();
    }
    const auto renderPass = renderPassResult.value();
    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = renderPass,
        .attachmentCount = 1,
        .pAttachments = &view,
        .width = command.extent.width,
        .height = command.extent.height,
        .layers = 1,
    };
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    result = context.deviceTable.vkCreateFramebuffer(
        context.device, &framebufferInfo, nullptr, &framebuffer);
    if (result != VK_SUCCESS) {
        context.deviceTable.vkDestroyRenderPass(context.device, renderPass,
                                                nullptr);
        context.deviceTable.vkDestroyImageView(context.device, view, nullptr);
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan framebuffer creation failed", result);
    }
    resources.attachmentViews.push_back(view);
    resources.renderPasses.push_back(renderPass);
    resources.framebuffers.push_back(framebuffer);
    const VkClearValue clear{{{attachment.clear.r, attachment.clear.g,
                               attachment.clear.b, attachment.clear.a}}};
    const VkRenderPassBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = renderPass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {command.extent.width, command.extent.height}},
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    context.deviceTable.vkCmdBeginRenderPass(
        commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    return Status::success();
}

[[nodiscard]] Result<VkDescriptorPool> create_vulkan_descriptor_pool(
    VulkanContext& context,
    std::span<const detail::NativeCommand> commands) {
    std::array<std::uint32_t, 5> counts{};
    std::uint32_t setCount = 0;
    for (const auto& command : commands) {
        if (command.kind != detail::NativeCommandKind::bind_group) {
            continue;
        }
        ++setCount;
        for (const auto& binding : command.bindings) {
            ++counts[static_cast<std::size_t>(binding.type)];
        }
    }
    if (setCount == 0) {
        return VkDescriptorPool{VK_NULL_HANDLE};
    }
    std::vector<VkDescriptorPoolSize> sizes;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        if (counts[index] == 0) {
            continue;
        }
        sizes.push_back({
            .type = vulkan_descriptor_type(static_cast<BindingType>(index)),
            .descriptorCount = counts[index],
        });
    }
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = setCount,
        .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data(),
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    const auto result = context.deviceTable.vkCreateDescriptorPool(
        context.device, &poolInfo, nullptr, &pool);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::out_of_memory,
                              "Vulkan descriptor-pool creation failed", result);
    }
    return pool;
}

[[nodiscard]] Status encode_vulkan_bind_group(
    VulkanContext& context, VkCommandBuffer commandBuffer,
    VkDescriptorPool descriptorPool,
    const VulkanPipelineResource& pipeline,
    const detail::NativeCommand& command) {
    const auto group = static_cast<std::uint32_t>(command.arguments[0]);
    if (descriptorPool == VK_NULL_HANDLE ||
        group >= pipeline.setLayouts.size()) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan bind-group set is unavailable");
    }
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &pipeline.setLayouts[group],
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    auto result = context.deviceTable.vkAllocateDescriptorSets(
        context.device, &allocateInfo, &set);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::out_of_memory,
                              "Vulkan descriptor-set allocation failed", result);
    }

    std::vector<VkDescriptorBufferInfo> bufferInfos(command.bindings.size());
    std::vector<VkDescriptorImageInfo> imageInfos(command.bindings.size());
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(command.bindings.size());
    for (std::size_t index = 0; index < command.bindings.size(); ++index) {
        const auto& binding = command.bindings[index];
        if (binding.group != group) {
            return Status::failure(StatusCode::invalid_argument,
                                   "Vulkan bind-group resources disagree on set");
        }
        VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = set,
            .dstBinding = binding.binding,
            .dstArrayElement = binding.arrayElement,
            .descriptorCount = 1,
            .descriptorType = vulkan_descriptor_type(binding.type),
            .pImageInfo = nullptr,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };
        switch (binding.type) {
        case BindingType::uniform_buffer:
        case BindingType::storage_buffer: {
            const auto buffer =
                std::static_pointer_cast<VulkanBufferResource>(binding.resource);
            if (!buffer || buffer->buffer == VK_NULL_HANDLE ||
                binding.offset > buffer->logicalSize ||
                binding.size > buffer->logicalSize - binding.offset) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Vulkan buffer descriptor is invalid");
            }
            bufferInfos[index] = {
                .buffer = buffer->buffer,
                .offset = static_cast<VkDeviceSize>(binding.offset),
                .range = static_cast<VkDeviceSize>(binding.size),
            };
            write.pBufferInfo = &bufferInfos[index];
            break;
        }
        case BindingType::sampled_texture:
        case BindingType::storage_texture: {
            const auto view = std::static_pointer_cast<VulkanTextureViewResource>(
                binding.resource);
            if (!view || view->view == VK_NULL_HANDLE || !view->texture) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Vulkan image descriptor is invalid");
            }
            const auto layout = binding.type == BindingType::sampled_texture
                                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                    : VK_IMAGE_LAYOUT_GENERAL;
            const auto& range = view->desc.range;
            for (std::uint32_t layer = 0; layer < range.arrayLayerCount;
                 ++layer) {
                for (std::uint32_t mip = 0; mip < range.mipLevelCount; ++mip) {
                    transition_vulkan_texture(
                        context, commandBuffer, *view->texture,
                        {.aspect = range.aspects,
                         .mipLevel = range.baseMipLevel + mip,
                         .arrayLayer = range.baseArrayLayer + layer},
                        layout);
                }
            }
            imageInfos[index] = {
                .sampler = VK_NULL_HANDLE,
                .imageView = view->view,
                .imageLayout = layout,
            };
            write.pImageInfo = &imageInfos[index];
            break;
        }
        case BindingType::sampler: {
            const auto sampler =
                std::static_pointer_cast<VulkanSamplerResource>(binding.resource);
            if (!sampler || sampler->sampler == VK_NULL_HANDLE) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Vulkan sampler descriptor is invalid");
            }
            imageInfos[index] = {
                .sampler = sampler->sampler,
                .imageView = VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };
            write.pImageInfo = &imageInfos[index];
            break;
        }
        }
        writes.push_back(write);
    }
    context.deviceTable.vkUpdateDescriptorSets(
        context.device, static_cast<std::uint32_t>(writes.size()),
        writes.data(), 0, nullptr);
    context.deviceTable.vkCmdBindDescriptorSets(
        commandBuffer, pipeline.bindPoint,
        pipeline.layout, group, 1, &set, 0, nullptr);
    return Status::success();
}

[[nodiscard]] VkBufferImageCopy vulkan_buffer_image_copy(
    const BufferTextureCopyRegion& region,
    const VulkanTextureResource& texture) {
    const auto rowLength =
        region.layout.bytesPerRow == 0
            ? 0u
            : static_cast<std::uint32_t>(region.layout.bytesPerRow /
                                         texture.format.bytesPerBlock) *
                  texture.format.blockWidth;
    const auto imageHeight =
        region.layout.rowsPerImage == 0
            ? 0u
            : static_cast<std::uint32_t>(region.layout.rowsPerImage) *
                  texture.format.blockHeight;
    return {
        .bufferOffset =
            static_cast<VkDeviceSize>(region.bufferOffset +
                                      region.layout.offset),
        .bufferRowLength = rowLength,
        .bufferImageHeight = imageHeight,
        .imageSubresource = {
            .aspectMask = vulkan_aspect(region.texture.subresource.aspect),
            .mipLevel = region.texture.subresource.mipLevel,
            .baseArrayLayer = region.texture.subresource.arrayLayer,
            .layerCount = 1,
        },
        .imageOffset = {
            static_cast<std::int32_t>(region.texture.origin.x),
            static_cast<std::int32_t>(region.texture.origin.y),
            static_cast<std::int32_t>(region.texture.origin.z),
        },
        .imageExtent = {region.texture.extent.width,
                        region.texture.extent.height,
                        region.texture.extent.depth},
    };
}

[[nodiscard]] Status record_vulkan_commands(
    VulkanContext& context, VkCommandBuffer commandBuffer,
    VkDescriptorPool descriptorPool,
    std::span<const detail::NativeCommand> commands,
    VulkanSubmissionResources& submissionResources) {
    bool recordedTransfer = false;
    bool recordedComputeWrite = false;
    bool recordedRenderWrite = false;
    std::shared_ptr<VulkanPipelineResource> computePipeline;
    std::shared_ptr<VulkanPipelineResource> graphicsPipeline;
    Extent2D renderExtent{};
    const auto memory_barrier = [&](VkPipelineStageFlags sourceStages,
                                    VkAccessFlags sourceAccess,
                                    VkPipelineStageFlags destinationStages,
                                    VkAccessFlags destinationAccess) {
        const VkMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = sourceAccess,
            .dstAccessMask = destinationAccess,
        };
        context.deviceTable.vkCmdPipelineBarrier(
            commandBuffer, sourceStages, destinationStages, 0, 1, &barrier, 0,
            nullptr, 0, nullptr);
    };
    for (const auto& command : commands) {
        if (command.kind == detail::NativeCommandKind::barrier) {
            continue;
        }
        if (command.kind != detail::NativeCommandKind::transfer) {
            switch (command.kind) {
            case detail::NativeCommandKind::begin_render:
                graphicsPipeline.reset();
                renderExtent = command.extent;
                if (auto status = begin_vulkan_render_pass(
                        context, commandBuffer, command, submissionResources);
                    !status.ok()) {
                    return status;
                }
                break;
            case detail::NativeCommandKind::end_render:
                context.deviceTable.vkCmdEndRenderPass(commandBuffer);
                graphicsPipeline.reset();
                recordedRenderWrite = true;
                break;
            case detail::NativeCommandKind::begin_compute:
                computePipeline.reset();
                break;
            case detail::NativeCommandKind::end_compute:
                computePipeline.reset();
                break;
            case detail::NativeCommandKind::bind_graphics_pipeline:
                graphicsPipeline =
                    std::static_pointer_cast<VulkanPipelineResource>(
                        command.object);
                context.deviceTable.vkCmdBindPipeline(
                    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    graphicsPipeline->pipeline);
                if (graphicsPipeline->automaticViewport) {
                    const VkViewport viewport{0.0F, 0.0F,
                                              static_cast<float>(
                                                  renderExtent.width),
                                              static_cast<float>(
                                                  renderExtent.height),
                                              0.0F, 1.0F};
                    context.deviceTable.vkCmdSetViewport(commandBuffer, 0, 1,
                                                         &viewport);
                }
                if (graphicsPipeline->automaticScissor) {
                    const VkRect2D scissor{{0, 0},
                                           {renderExtent.width,
                                            renderExtent.height}};
                    context.deviceTable.vkCmdSetScissor(commandBuffer, 0, 1,
                                                        &scissor);
                }
                break;
            case detail::NativeCommandKind::bind_compute_pipeline:
                computePipeline =
                    std::static_pointer_cast<VulkanPipelineResource>(
                        command.object);
                context.deviceTable.vkCmdBindPipeline(
                    commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    computePipeline->pipeline);
                break;
            case detail::NativeCommandKind::bind_group:
                if (!computePipeline && !graphicsPipeline) {
                    return Status::failure(
                        StatusCode::invalid_state,
                        "Vulkan bind group requires a pipeline");
                }
                if (auto status = encode_vulkan_bind_group(
                        context, commandBuffer, descriptorPool,
                        graphicsPipeline ? *graphicsPipeline : *computePipeline,
                        command);
                    !status.ok()) {
                    return status;
                }
                break;
            case detail::NativeCommandKind::push_constants:
                if (!computePipeline && !graphicsPipeline) {
                    return Status::failure(
                        StatusCode::invalid_state,
                        "Vulkan push constants require a pipeline");
                }
                {
                    const auto& pipeline =
                        graphicsPipeline ? graphicsPipeline : computePipeline;
                context.deviceTable.vkCmdPushConstants(
                    commandBuffer, pipeline->layout,
                    vulkan_shader_stages(static_cast<ShaderStageMask>(
                        command.arguments[0])),
                    static_cast<std::uint32_t>(command.arguments[1]),
                    static_cast<std::uint32_t>(command.bytes.size()),
                    command.bytes.data());
                }
                break;
            case detail::NativeCommandKind::bind_vertex_buffer: {
                const auto buffer =
                    std::static_pointer_cast<VulkanBufferResource>(
                        command.object);
                const auto nativeOffset =
                    static_cast<VkDeviceSize>(command.arguments[1]);
                context.deviceTable.vkCmdBindVertexBuffers(
                    commandBuffer,
                    static_cast<std::uint32_t>(command.arguments[0]), 1,
                    &buffer->buffer, &nativeOffset);
                break;
            }
            case detail::NativeCommandKind::bind_index_buffer: {
                const auto buffer =
                    std::static_pointer_cast<VulkanBufferResource>(
                        command.object);
                context.deviceTable.vkCmdBindIndexBuffer(
                    commandBuffer, buffer->buffer,
                    static_cast<VkDeviceSize>(command.arguments[0]),
                    static_cast<IndexFormat>(command.arguments[1]) ==
                            IndexFormat::uint16
                        ? VK_INDEX_TYPE_UINT16
                        : VK_INDEX_TYPE_UINT32);
                break;
            }
            case detail::NativeCommandKind::set_viewports: {
                std::vector<VkViewport> viewports;
                viewports.reserve(command.viewports.size());
                for (const auto& viewport : command.viewports) {
                    viewports.push_back({viewport.x, viewport.y,
                                         viewport.width, viewport.height,
                                         viewport.minimumDepth,
                                         viewport.maximumDepth});
                }
                context.deviceTable.vkCmdSetViewport(
                    commandBuffer,
                    static_cast<std::uint32_t>(command.arguments[0]),
                    static_cast<std::uint32_t>(viewports.size()),
                    viewports.data());
                break;
            }
            case detail::NativeCommandKind::set_scissors: {
                std::vector<VkRect2D> scissors;
                scissors.reserve(command.scissors.size());
                for (const auto& scissor : command.scissors) {
                    scissors.push_back({{scissor.x, scissor.y},
                                        {scissor.width, scissor.height}});
                }
                context.deviceTable.vkCmdSetScissor(
                    commandBuffer,
                    static_cast<std::uint32_t>(command.arguments[0]),
                    static_cast<std::uint32_t>(scissors.size()),
                    scissors.data());
                break;
            }
            case detail::NativeCommandKind::set_blend_constant: {
                std::array<float, 4> color{};
                std::memcpy(color.data(), command.bytes.data(),
                            sizeof(color));
                context.deviceTable.vkCmdSetBlendConstants(commandBuffer,
                                                            color.data());
                break;
            }
            case detail::NativeCommandKind::set_stencil_reference:
                context.deviceTable.vkCmdSetStencilReference(
                    commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK,
                    static_cast<std::uint32_t>(command.arguments[0]));
                break;
            case detail::NativeCommandKind::set_depth_bias: {
                std::array<float, 3> values{};
                std::memcpy(values.data(), command.bytes.data(),
                            sizeof(values));
                context.deviceTable.vkCmdSetDepthBias(
                    commandBuffer, values[0], values[2], values[1]);
                break;
            }
            case detail::NativeCommandKind::draw:
                context.deviceTable.vkCmdDraw(
                    commandBuffer,
                    static_cast<std::uint32_t>(command.arguments[0]),
                    static_cast<std::uint32_t>(command.arguments[1]),
                    static_cast<std::uint32_t>(command.arguments[2]),
                    static_cast<std::uint32_t>(command.arguments[3]));
                break;
            case detail::NativeCommandKind::draw_indexed:
                context.deviceTable.vkCmdDrawIndexed(
                    commandBuffer,
                    static_cast<std::uint32_t>(command.arguments[0]),
                    static_cast<std::uint32_t>(command.arguments[1]),
                    static_cast<std::uint32_t>(command.arguments[2]),
                    static_cast<std::int32_t>(command.arguments[3]),
                    static_cast<std::uint32_t>(command.arguments[4]));
                break;
            case detail::NativeCommandKind::dispatch:
                context.deviceTable.vkCmdDispatch(
                    commandBuffer,
                    static_cast<std::uint32_t>(command.arguments[0]),
                    static_cast<std::uint32_t>(command.arguments[1]),
                    static_cast<std::uint32_t>(command.arguments[2]));
                recordedComputeWrite = true;
                break;
            default:
                return Status::failure(
                    StatusCode::unsupported,
                    "this Vulkan command kind is not implemented");
            }
            continue;
        }
        const auto& transfer = command.transfer;
        if (recordedTransfer) {
            memory_barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_TRANSFER_WRITE_BIT);
        } else {
            const auto sourceStages =
                (recordedComputeWrite ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                      : 0u) |
                (recordedRenderWrite
                     ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     : 0u);
            const auto sourceAccess =
                (recordedComputeWrite ? VK_ACCESS_SHADER_WRITE_BIT : 0u) |
                (recordedRenderWrite
                     ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                     : 0u);
            memory_barrier(sourceStages != 0 ? sourceStages
                                             : VK_PIPELINE_STAGE_HOST_BIT,
                           sourceAccess != 0 ? sourceAccess
                                             : VK_ACCESS_HOST_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_TRANSFER_WRITE_BIT);
            recordedTransfer = true;
        }
        switch (transfer.kind) {
        case detail::NativeTransferKind::copy_buffer: {
            const auto source =
                std::static_pointer_cast<VulkanBufferResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanBufferResource>(
                    transfer.destination);
            if (!source || !destination || source->buffer == VK_NULL_HANDLE ||
                destination->buffer == VK_NULL_HANDLE) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Vulkan buffer copy resources are invalid");
            }
            if ((transfer.buffer.sourceOffset & 3u) != 0 ||
                (transfer.buffer.destinationOffset & 3u) != 0 ||
                (transfer.buffer.size & 3u) != 0) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan buffer copy offsets and size must be four-byte aligned");
            }
            const VkBufferCopy region{
                .srcOffset =
                    static_cast<VkDeviceSize>(transfer.buffer.sourceOffset),
                .dstOffset = static_cast<VkDeviceSize>(
                    transfer.buffer.destinationOffset),
                .size = static_cast<VkDeviceSize>(transfer.buffer.size),
            };
            context.deviceTable.vkCmdCopyBuffer(commandBuffer, source->buffer,
                                                 destination->buffer, 1,
                                                 &region);
            break;
        }
        case detail::NativeTransferKind::fill_buffer: {
            const auto destination =
                std::static_pointer_cast<VulkanBufferResource>(
                    transfer.destination);
            if (!destination || destination->buffer == VK_NULL_HANDLE) {
                return Status::failure(StatusCode::invalid_argument,
                                       "Vulkan buffer fill resource is invalid");
            }
            if ((transfer.buffer.destinationOffset & 3u) != 0 ||
                (transfer.buffer.size & 3u) != 0) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "Vulkan buffer fill offset and size must be four-byte aligned");
            }
            const auto byte =
                static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(
                    transfer.fillValue));
            const auto pattern = byte * 0x01010101u;
            context.deviceTable.vkCmdFillBuffer(
                commandBuffer, destination->buffer,
                static_cast<VkDeviceSize>(transfer.buffer.destinationOffset),
                static_cast<VkDeviceSize>(transfer.buffer.size), pattern);
            break;
        }
        case detail::NativeTransferKind::copy_buffer_to_texture: {
            const auto source =
                std::static_pointer_cast<VulkanBufferResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            transition_vulkan_texture(
                context, commandBuffer, *destination,
                transfer.bufferTexture.texture.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const auto region =
                vulkan_buffer_image_copy(transfer.bufferTexture, *destination);
            context.deviceTable.vkCmdCopyBufferToImage(
                commandBuffer, source->buffer, destination->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            break;
        }
        case detail::NativeTransferKind::copy_texture_to_buffer: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanBufferResource>(
                    transfer.destination);
            transition_vulkan_texture(
                context, commandBuffer, *source,
                transfer.bufferTexture.texture.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            const auto region =
                vulkan_buffer_image_copy(transfer.bufferTexture, *source);
            context.deviceTable.vkCmdCopyImageToBuffer(
                commandBuffer, source->image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->buffer, 1,
                &region);
            break;
        }
        case detail::NativeTransferKind::copy_texture: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            transition_vulkan_texture(
                context, commandBuffer, *source,
                transfer.texture.source.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            transition_vulkan_texture(
                context, commandBuffer, *destination,
                transfer.texture.destination.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkImageCopy region{
                .srcSubresource = {
                    .aspectMask = vulkan_aspect(
                        transfer.texture.source.subresource.aspect),
                    .mipLevel =
                        transfer.texture.source.subresource.mipLevel,
                    .baseArrayLayer =
                        transfer.texture.source.subresource.arrayLayer,
                    .layerCount = 1,
                },
                .srcOffset = {
                    static_cast<std::int32_t>(
                        transfer.texture.source.origin.x),
                    static_cast<std::int32_t>(
                        transfer.texture.source.origin.y),
                    static_cast<std::int32_t>(
                        transfer.texture.source.origin.z),
                },
                .dstSubresource = {
                    .aspectMask = vulkan_aspect(
                        transfer.texture.destination.subresource.aspect),
                    .mipLevel =
                        transfer.texture.destination.subresource.mipLevel,
                    .baseArrayLayer =
                        transfer.texture.destination.subresource.arrayLayer,
                    .layerCount = 1,
                },
                .dstOffset = {
                    static_cast<std::int32_t>(
                        transfer.texture.destination.origin.x),
                    static_cast<std::int32_t>(
                        transfer.texture.destination.origin.y),
                    static_cast<std::int32_t>(
                        transfer.texture.destination.origin.z),
                },
                .extent = {transfer.texture.source.extent.width,
                           transfer.texture.source.extent.height,
                           transfer.texture.source.extent.depth},
            };
            context.deviceTable.vkCmdCopyImage(
                commandBuffer, source->image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            break;
        }
        case detail::NativeTransferKind::clear_texture: {
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            const auto& region = transfer.texture.destination;
            transition_vulkan_texture(context, commandBuffer, *destination,
                                      region.subresource,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkImageSubresourceRange range{
                .aspectMask = vulkan_aspect(region.subresource.aspect),
                .baseMipLevel = region.subresource.mipLevel,
                .levelCount = 1,
                .baseArrayLayer = region.subresource.arrayLayer,
                .layerCount = 1,
            };
            if (destination->format.aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
                const VkClearColorValue value{{transfer.clear.color.r,
                                               transfer.clear.color.g,
                                               transfer.clear.color.b,
                                               transfer.clear.color.a}};
                context.deviceTable.vkCmdClearColorImage(
                    commandBuffer, destination->image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
            } else {
                const VkClearDepthStencilValue value{
                    transfer.clear.depth, transfer.clear.stencil};
                context.deviceTable.vkCmdClearDepthStencilImage(
                    commandBuffer, destination->image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
            }
            break;
        }
        case detail::NativeTransferKind::resolve_texture: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            transition_vulkan_texture(
                context, commandBuffer, *source,
                transfer.texture.source.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            transition_vulkan_texture(
                context, commandBuffer, *destination,
                transfer.texture.destination.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkImageResolve region{
                .srcSubresource = {
                    .aspectMask = vulkan_aspect(
                        transfer.texture.source.subresource.aspect),
                    .mipLevel = transfer.texture.source.subresource.mipLevel,
                    .baseArrayLayer =
                        transfer.texture.source.subresource.arrayLayer,
                    .layerCount = 1,
                },
                .srcOffset = {
                    static_cast<std::int32_t>(transfer.texture.source.origin.x),
                    static_cast<std::int32_t>(transfer.texture.source.origin.y),
                    static_cast<std::int32_t>(transfer.texture.source.origin.z),
                },
                .dstSubresource = {
                    .aspectMask = vulkan_aspect(
                        transfer.texture.destination.subresource.aspect),
                    .mipLevel =
                        transfer.texture.destination.subresource.mipLevel,
                    .baseArrayLayer =
                        transfer.texture.destination.subresource.arrayLayer,
                    .layerCount = 1,
                },
                .dstOffset = {
                    static_cast<std::int32_t>(
                        transfer.texture.destination.origin.x),
                    static_cast<std::int32_t>(
                        transfer.texture.destination.origin.y),
                    static_cast<std::int32_t>(
                        transfer.texture.destination.origin.z),
                },
                .extent = {transfer.texture.source.extent.width,
                           transfer.texture.source.extent.height,
                           transfer.texture.source.extent.depth},
            };
            context.deviceTable.vkCmdResolveImage(
                commandBuffer, source->image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            break;
        }
        case detail::NativeTransferKind::blit_texture: {
            const auto source =
                std::static_pointer_cast<VulkanTextureResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<VulkanTextureResource>(
                    transfer.destination);
            transition_vulkan_texture(
                context, commandBuffer, *source,
                transfer.blit.source.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            transition_vulkan_texture(
                context, commandBuffer, *destination,
                transfer.blit.destination.subresource,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const auto sourceEnd = transfer.blit.source.origin;
            const auto destinationEnd = transfer.blit.destination.origin;
            const VkImageBlit region{
                .srcSubresource = {
                    .aspectMask = vulkan_aspect(
                        transfer.blit.source.subresource.aspect),
                    .mipLevel = transfer.blit.source.subresource.mipLevel,
                    .baseArrayLayer =
                        transfer.blit.source.subresource.arrayLayer,
                    .layerCount = 1,
                },
                .srcOffsets = {
                    {static_cast<std::int32_t>(transfer.blit.source.origin.x),
                     static_cast<std::int32_t>(transfer.blit.source.origin.y),
                     static_cast<std::int32_t>(transfer.blit.source.origin.z)},
                    {static_cast<std::int32_t>(
                         sourceEnd.x + transfer.blit.source.extent.width),
                     static_cast<std::int32_t>(
                         sourceEnd.y + transfer.blit.source.extent.height),
                     static_cast<std::int32_t>(
                         sourceEnd.z + transfer.blit.source.extent.depth)}},
                .dstSubresource = {
                    .aspectMask = vulkan_aspect(
                        transfer.blit.destination.subresource.aspect),
                    .mipLevel = transfer.blit.destination.subresource.mipLevel,
                    .baseArrayLayer =
                        transfer.blit.destination.subresource.arrayLayer,
                    .layerCount = 1,
                },
                .dstOffsets = {
                    {static_cast<std::int32_t>(
                         transfer.blit.destination.origin.x),
                     static_cast<std::int32_t>(
                         transfer.blit.destination.origin.y),
                     static_cast<std::int32_t>(
                         transfer.blit.destination.origin.z)},
                    {static_cast<std::int32_t>(
                         destinationEnd.x +
                         transfer.blit.destination.extent.width),
                     static_cast<std::int32_t>(
                         destinationEnd.y +
                         transfer.blit.destination.extent.height),
                     static_cast<std::int32_t>(
                         destinationEnd.z +
                         transfer.blit.destination.extent.depth)}},
            };
            context.deviceTable.vkCmdBlitImage(
                commandBuffer, source->image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                transfer.blit.filter == Filter::linear ? VK_FILTER_LINEAR
                                                       : VK_FILTER_NEAREST);
            break;
        }
        }
    }
    if (recordedTransfer) {
        memory_barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    } else if (recordedComputeWrite) {
        memory_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_ACCESS_HOST_READ_BIT);
    }
    return Status::success();
}

[[nodiscard]] Status submit_vulkan_command_buffer(
    VulkanContext& context, std::span<const detail::NativeCommand> commands) {
    if (auto status = validate_vulkan_commands(context, commands); !status.ok()) {
        return status;
    }
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context.queueFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    auto result = context.deviceTable.vkCreateCommandPool(
        context.device, &poolInfo, nullptr, &pool);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan command-pool creation failed", result);
    }

    VkCommandBufferAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocationInfo.commandPool = pool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    result = context.deviceTable.vkAllocateCommandBuffers(
        context.device, &allocationInfo, &commandBuffer);
    if (result == VK_SUCCESS) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = context.deviceTable.vkBeginCommandBuffer(commandBuffer,
                                                          &beginInfo);
    }
    if (result == VK_SUCCESS) {
        auto descriptorPoolResult =
            create_vulkan_descriptor_pool(context, commands);
        if (!descriptorPoolResult.ok()) {
            context.deviceTable.vkDestroyCommandPool(context.device, pool,
                                                      nullptr);
            return descriptorPoolResult.status();
        }
        const auto descriptorPool = descriptorPoolResult.value();
        VulkanSubmissionResources submissionResources;
        if (auto status = record_vulkan_commands(context, commandBuffer,
                                                 descriptorPool,
                                                 commands,
                                                 submissionResources);
            !status.ok()) {
            destroy_vulkan_submission_resources(context,
                                                submissionResources);
            if (descriptorPool != VK_NULL_HANDLE) {
                context.deviceTable.vkDestroyDescriptorPool(
                    context.device, descriptorPool, nullptr);
            }
            context.deviceTable.vkDestroyCommandPool(context.device, pool,
                                                      nullptr);
            return status;
        }
        if (result == VK_SUCCESS) {
            result = context.deviceTable.vkEndCommandBuffer(commandBuffer);
        }
        if (result == VK_SUCCESS) {
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            result = context.deviceTable.vkQueueSubmit(
                context.queue, 1, &submitInfo, VK_NULL_HANDLE);
        }
        if (result == VK_SUCCESS) {
            result = context.deviceTable.vkQueueWaitIdle(context.queue);
        }
        if (descriptorPool != VK_NULL_HANDLE) {
            context.deviceTable.vkDestroyDescriptorPool(
                context.device, descriptorPool, nullptr);
        }
        destroy_vulkan_submission_resources(context, submissionResources);
    }
    context.deviceTable.vkDestroyCommandPool(context.device, pool, nullptr);
    if (result == VK_ERROR_DEVICE_LOST) {
        return vulkan_failure(StatusCode::device_lost,
                              "Vulkan lost the device during command submission",
                              result);
    }
    return result == VK_SUCCESS
               ? Status::success()
               : vulkan_failure(StatusCode::backend_error,
                                "Vulkan native command submission failed", result);
}

[[nodiscard]] bool vulkan_host_texture_region_valid(
    const VulkanTextureResource& texture, const TextureRegion& region) noexcept {
    if (!texture.hostVisible || texture.mapped == nullptr ||
        region.subresource.mipLevel >= texture.desc.mipLevels ||
        region.subresource.arrayLayer >= texture.desc.arrayLayers ||
        !vulkan_single_aspect(region.subresource.aspect) ||
        (texture.format.aspects & vulkan_aspect(region.subresource.aspect)) == 0 ||
        region.extent.width == 0 || region.extent.height == 0 ||
        region.extent.depth == 0) {
        return false;
    }
    const auto mip = region.subresource.mipLevel;
    const auto width = mip_dimension(texture.desc.extent.width, mip);
    const auto height = mip_dimension(texture.desc.extent.height, mip);
    const auto depth = texture.desc.dimension == TextureDimension::d3
                           ? mip_dimension(texture.desc.extent.depth, mip)
                           : 1u;
    if (region.origin.x > width || region.extent.width > width - region.origin.x ||
        region.origin.y > height ||
        region.extent.height > height - region.origin.y ||
        region.origin.z > depth ||
        region.extent.depth > depth - region.origin.z) {
        return false;
    }
    return region.origin.x % texture.format.blockWidth == 0 &&
           region.origin.y % texture.format.blockHeight == 0;
}

[[nodiscard]] Status transition_vulkan_texture_to_host(
    VulkanContext& context, VulkanTextureResource& texture,
    const TextureSubresource& subresource, bool write) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context.queueFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    auto result = context.deviceTable.vkCreateCommandPool(
        context.device, &poolInfo, nullptr, &pool);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan host-transition pool creation failed",
                              result);
    }
    VkCommandBufferAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocationInfo.commandPool = pool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    result = context.deviceTable.vkAllocateCommandBuffers(
        context.device, &allocationInfo, &commandBuffer);
    if (result == VK_SUCCESS) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = context.deviceTable.vkBeginCommandBuffer(commandBuffer,
                                                          &beginInfo);
    }
    if (result == VK_SUCCESS) {
        std::lock_guard textureLock{texture.mutex};
        auto& oldLayout = texture.layouts[texture.layout_index(
            subresource.mipLevel, subresource.arrayLayer)];
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = vulkan_layout_access(oldLayout),
            .dstAccessMask = write ? VK_ACCESS_HOST_WRITE_BIT
                                   : VK_ACCESS_HOST_READ_BIT,
            .oldLayout = oldLayout,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture.image,
            .subresourceRange = {
                .aspectMask = vulkan_aspect(subresource.aspect),
                .baseMipLevel = subresource.mipLevel,
                .levelCount = 1,
                .baseArrayLayer = subresource.arrayLayer,
                .layerCount = 1,
            },
        };
        context.deviceTable.vkCmdPipelineBarrier(
            commandBuffer, vulkan_layout_stage(oldLayout),
            VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        result = context.deviceTable.vkEndCommandBuffer(commandBuffer);
    }
    if (result == VK_SUCCESS) {
        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
        };
        result = context.deviceTable.vkQueueSubmit(context.queue, 1, &submitInfo,
                                                   VK_NULL_HANDLE);
    }
    if (result == VK_SUCCESS) {
        result = context.deviceTable.vkQueueWaitIdle(context.queue);
    }
    if (result == VK_SUCCESS) {
        std::lock_guard textureLock{texture.mutex};
        texture.layouts[texture.layout_index(subresource.mipLevel,
                                             subresource.arrayLayer)] =
            VK_IMAGE_LAYOUT_GENERAL;
    }
    context.deviceTable.vkDestroyCommandPool(context.device, pool, nullptr);
    if (result == VK_ERROR_DEVICE_LOST) {
        return vulkan_failure(StatusCode::device_lost,
                              "Vulkan lost the device during host transition",
                              result);
    }
    return result == VK_SUCCESS
               ? Status::success()
               : vulkan_failure(StatusCode::backend_error,
                                "Vulkan host texture transition failed", result);
}

[[nodiscard]] Status access_vulkan_texture_host_memory(
    VulkanTextureResource& texture, const TextureRegion& region,
    const TextureDataLayout& dataLayout, const std::byte* source,
    std::byte* destination, std::size_t dataSize, bool write) {
    if (!vulkan_host_texture_region_valid(texture, region)) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan host texture region is invalid");
    }
    const auto blocksWide =
        divide_round_up(region.extent.width, texture.format.blockWidth);
    const auto blocksHigh =
        divide_round_up(region.extent.height, texture.format.blockHeight);
    const auto tightRow = static_cast<std::size_t>(blocksWide) *
                          texture.format.bytesPerBlock;
    const auto dataRow =
        dataLayout.bytesPerRow == 0 ? tightRow : dataLayout.bytesPerRow;
    const auto rowsPerImage = dataLayout.rowsPerImage == 0
                                  ? static_cast<std::size_t>(blocksHigh)
                                  : dataLayout.rowsPerImage;
    if (dataRow < tightRow || rowsPerImage < blocksHigh ||
        dataLayout.offset > dataSize) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan host texture data layout is invalid");
    }
    const auto available = dataSize - dataLayout.offset;
    const auto precedingImages =
        static_cast<std::size_t>(region.extent.depth - 1u);
    const auto precedingRows = static_cast<std::size_t>(blocksHigh - 1u);
    if (precedingImages != 0 &&
        (rowsPerImage > std::numeric_limits<std::size_t>::max() / dataRow ||
         dataRow * rowsPerImage > available / precedingImages)) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan host texture data is too small");
    }
    const auto imageStride = precedingImages == 0 ? 0 : dataRow * rowsPerImage;
    const auto imageOffset = precedingImages * imageStride;
    if (precedingRows > (available - imageOffset) / dataRow ||
        tightRow > available - imageOffset - precedingRows * dataRow) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan host texture data is too small");
    }

    auto& context = *texture.context;
    std::lock_guard contextLock{context.mutex};
    if (auto status = transition_vulkan_texture_to_host(
            context, texture, region.subresource, write);
        !status.ok()) {
        return status;
    }
    std::lock_guard textureLock{texture.mutex};
    const VkImageSubresource subresource{
        .aspectMask = vulkan_aspect(region.subresource.aspect),
        .mipLevel = region.subresource.mipLevel,
        .arrayLayer = region.subresource.arrayLayer,
    };
    VkSubresourceLayout nativeLayout{};
    context.deviceTable.vkGetImageSubresourceLayout(
        context.device, texture.image, &subresource, &nativeLayout);
    const auto blockX = region.origin.x / texture.format.blockWidth;
    const auto blockY = region.origin.y / texture.format.blockHeight;
    const auto nativeRows = divide_round_up(
        mip_dimension(texture.desc.extent.height,
                      region.subresource.mipLevel),
        texture.format.blockHeight);
    if (texture.mapped == nullptr || nativeLayout.rowPitch == 0 ||
        (nativeLayout.depthPitch == 0 &&
         nativeRows > std::numeric_limits<VkDeviceSize>::max() /
                          nativeLayout.rowPitch)) {
        return Status::failure(StatusCode::backend_error,
                               "Vulkan returned an invalid host texture layout");
    }
    const auto nativeDepthStride =
        nativeLayout.depthPitch == 0
            ? nativeLayout.rowPitch * nativeRows
            : nativeLayout.depthPitch;
    const auto advance_within_allocation =
        [&](VkDeviceSize& cursor, VkDeviceSize count,
            VkDeviceSize stride) noexcept {
            if (cursor > texture.allocationSize ||
                (count != 0 &&
                 stride > (texture.allocationSize - cursor) / count)) {
                return false;
            }
            cursor += count * stride;
            return true;
        };
    auto nativeOffset = nativeLayout.offset;
    if (!advance_within_allocation(nativeOffset, region.origin.z,
                                   nativeDepthStride) ||
        !advance_within_allocation(nativeOffset, blockY,
                                   nativeLayout.rowPitch) ||
        !advance_within_allocation(nativeOffset, blockX,
                                   texture.format.bytesPerBlock)) {
        return Status::failure(StatusCode::backend_error,
                               "Vulkan host texture layout exceeds allocation");
    }
    auto nativeRequired = nativeOffset;
    if (!advance_within_allocation(nativeRequired, region.extent.depth - 1u,
                                   nativeDepthStride) ||
        !advance_within_allocation(nativeRequired, blocksHigh - 1u,
                                   nativeLayout.rowPitch) ||
        !advance_within_allocation(nativeRequired, 1, tightRow)) {
        return Status::failure(StatusCode::backend_error,
                               "Vulkan host texture layout exceeds allocation");
    }
    if (!write && !texture.hostCoherent) {
        const VkMappedMemoryRange range{
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = texture.memory,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        const auto result = context.deviceTable.vkInvalidateMappedMemoryRanges(
            context.device, 1, &range);
        if (result != VK_SUCCESS) {
            return vulkan_failure(StatusCode::backend_error,
                                  "Vulkan texture invalidation failed", result);
        }
    }
    auto* mapped = static_cast<std::byte*>(texture.mapped);
    for (std::uint32_t z = 0; z < region.extent.depth; ++z) {
        for (std::uint32_t row = 0; row < blocksHigh; ++row) {
            auto* native = mapped + nativeOffset +
                           static_cast<VkDeviceSize>(z) * nativeDepthStride +
                           static_cast<VkDeviceSize>(row) * nativeLayout.rowPitch;
            auto* data = (write ? const_cast<std::byte*>(source) : destination) +
                         dataLayout.offset +
                         static_cast<std::size_t>(z) * imageStride +
                         static_cast<std::size_t>(row) * dataRow;
            if (write) {
                std::memcpy(native, data, tightRow);
            } else {
                std::memcpy(data, native, tightRow);
            }
        }
    }
    if (write && !texture.hostCoherent) {
        const VkMappedMemoryRange range{
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = texture.memory,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        const auto result = context.deviceTable.vkFlushMappedMemoryRanges(
            context.device, 1, &range);
        if (result != VK_SUCCESS) {
            return vulkan_failure(StatusCode::backend_error,
                                  "Vulkan texture flush failed", result);
        }
    }
    return Status::success();
}

[[nodiscard]] Status write_vulkan_texture(
    const std::shared_ptr<void>& nativeResource, const TextureRegion& region,
    std::span<const std::byte> data, const TextureDataLayout& layout) {
    const auto resource =
        std::static_pointer_cast<VulkanTextureResource>(nativeResource);
    if (!resource || !resource->hostVisible) {
        return Status::failure(StatusCode::unsupported,
                               "Vulkan texture is not host writable");
    }
    return access_vulkan_texture_host_memory(*resource, region, layout,
                                             data.data(), nullptr, data.size(),
                                             true);
}

[[nodiscard]] Status read_vulkan_texture(
    const std::shared_ptr<void>& nativeResource, const TextureRegion& region,
    std::span<std::byte> data, const TextureDataLayout& layout) {
    const auto resource =
        std::static_pointer_cast<VulkanTextureResource>(nativeResource);
    if (!resource || !resource->hostVisible) {
        return Status::failure(StatusCode::unsupported,
                               "Vulkan texture is not host readable");
    }
    return access_vulkan_texture_host_memory(*resource, region, layout, nullptr,
                                             data.data(), data.size(), false);
}

[[nodiscard]] Result<VulkanProbe> initialize_vulkan(const InstanceDesc& desc) {
    const auto loaderResult = volkInitialize();
    if (loaderResult != VK_SUCCESS) {
        return vulkan_failure(StatusCode::unavailable,
                              "the native Vulkan loader is unavailable",
                              loaderResult);
    }

    std::vector<const char*> layers;
    if (desc.enableValidation) {
        std::uint32_t layerCount = 0;
        if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) ==
                VK_SUCCESS &&
            layerCount != 0) {
            std::vector<VkLayerProperties> availableLayers(layerCount);
            if (vkEnumerateInstanceLayerProperties(&layerCount,
                                                   availableLayers.data()) ==
                VK_SUCCESS) {
                const auto validation = std::find_if(
                    availableLayers.begin(), availableLayers.end(),
                    [](const VkLayerProperties& layer) {
                        return std::strcmp(layer.layerName,
                                           "VK_LAYER_KHRONOS_validation") == 0;
                    });
                if (validation != availableLayers.end()) {
                    layers.push_back("VK_LAYER_KHRONOS_validation");
                }
            }
        }
    }

    const VkApplicationInfo applicationInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Truffle RHI doctor",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "Truffle",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };
    std::vector<const char*> extensions;
    std::uint32_t extensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                               nullptr) == VK_SUCCESS &&
        extensionCount != 0) {
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        if (vkEnumerateInstanceExtensionProperties(
                nullptr, &extensionCount, availableExtensions.data()) ==
            VK_SUCCESS) {
            const auto portability = std::find_if(
                availableExtensions.begin(), availableExtensions.end(),
                [](const VkExtensionProperties& extension) {
                    return std::strcmp(
                               extension.extensionName,
                               VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) ==
                           0;
                });
            if (portability != availableExtensions.end()) {
                extensions.push_back(
                    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            }
        }
    }
    const auto instanceFlags =
        extensions.empty() ? VkInstanceCreateFlags{0}
                           : VkInstanceCreateFlags{
                                 VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR};
    const VkInstanceCreateInfo instanceInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = instanceFlags,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount =
            static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    auto context = std::make_shared<VulkanContext>();
    auto result = vkCreateInstance(&instanceInfo, nullptr, &context->instance);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::unavailable,
                              "Vulkan instance creation failed", result);
    }
    volkLoadInstanceTable(&context->instanceTable, context->instance);

    std::uint32_t physicalDeviceCount = 0;
    result = context->instanceTable.vkEnumeratePhysicalDevices(
        context->instance, &physicalDeviceCount, nullptr);
    if (result != VK_SUCCESS || physicalDeviceCount == 0) {
        return vulkan_failure(StatusCode::unavailable,
                              "Vulkan reported no physical adapters", result);
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    result = context->instanceTable.vkEnumeratePhysicalDevices(
        context->instance, &physicalDeviceCount, physicalDevices.data());
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::backend_error,
                              "Vulkan adapter enumeration failed", result);
    }

    bool foundQueue = false;
    for (const auto physicalDevice : physicalDevices) {
        std::uint32_t queueFamilyCount = 0;
        context->instanceTable.vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        context->instanceTable.vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &queueFamilyCount, queueFamilies.data());
        const auto graphics = std::find_if(
            queueFamilies.begin(), queueFamilies.end(),
            [](const VkQueueFamilyProperties& family) {
                return family.queueCount != 0 &&
                       (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            });
        if (graphics == queueFamilies.end()) {
            continue;
        }
        context->physicalDevice = physicalDevice;
        context->queueFamily = static_cast<std::uint32_t>(
            std::distance(queueFamilies.begin(), graphics));
        foundQueue = true;
        break;
    }
    if (!foundQueue) {
        return Status::failure(StatusCode::unavailable,
                               "Vulkan reported no graphics queue family");
    }

    VkPhysicalDeviceFeatures availableFeatures{};
    context->instanceTable.vkGetPhysicalDeviceFeatures(
        context->physicalDevice, &availableFeatures);
    VkPhysicalDeviceFeatures enabledFeatures{};
    enabledFeatures.textureCompressionBC =
        availableFeatures.textureCompressionBC;

    constexpr float queuePriority = 1.0F;
    const VkDeviceQueueCreateInfo queueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = context->queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    std::vector<const char*> deviceExtensions;
    std::uint32_t deviceExtensionCount = 0;
    if (context->instanceTable.vkEnumerateDeviceExtensionProperties(
            context->physicalDevice, nullptr, &deviceExtensionCount, nullptr) ==
            VK_SUCCESS &&
        deviceExtensionCount != 0) {
        std::vector<VkExtensionProperties> availableDeviceExtensions(
            deviceExtensionCount);
        if (context->instanceTable.vkEnumerateDeviceExtensionProperties(
                context->physicalDevice, nullptr, &deviceExtensionCount,
                availableDeviceExtensions.data()) == VK_SUCCESS) {
            const auto portability = std::find_if(
                availableDeviceExtensions.begin(),
                availableDeviceExtensions.end(),
                [](const VkExtensionProperties& extension) {
                    return std::strcmp(
                               extension.extensionName,
                               portability_subset_extension) == 0;
                });
            if (portability != availableDeviceExtensions.end()) {
                deviceExtensions.push_back(portability_subset_extension);
            }
        }
    }
    const VkDeviceCreateInfo deviceInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount =
            static_cast<std::uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = &enabledFeatures,
    };
    result = context->instanceTable.vkCreateDevice(
        context->physicalDevice, &deviceInfo, nullptr, &context->device);
    if (result != VK_SUCCESS) {
        return vulkan_failure(StatusCode::unavailable,
                              "Vulkan logical-device creation failed", result);
    }
    volkLoadDeviceTable(&context->deviceTable, context->device);
    context->deviceTable.vkGetDeviceQueue(context->device, context->queueFamily,
                                          0, &context->queue);

    if (auto status = submit_vulkan_command_buffer(*context, {}); !status.ok()) {
        return status;
    }

    context->instanceTable.vkGetPhysicalDeviceProperties(
        context->physicalDevice, &context->properties);
    context->instanceTable.vkGetPhysicalDeviceMemoryProperties(
        context->physicalDevice, &context->memoryProperties);
    VkDeviceSize deviceLocalBudget = 0;
    for (std::uint32_t index = 0;
         index < context->memoryProperties.memoryHeapCount; ++index) {
        if ((context->memoryProperties.memoryHeaps[index].flags &
             VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            deviceLocalBudget +=
                context->memoryProperties.memoryHeaps[index].size;
        }
    }
    bool hostCoherent = false;
    for (std::uint32_t index = 0;
         index < context->memoryProperties.memoryTypeCount; ++index) {
        constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((context->memoryProperties.memoryTypes[index].propertyFlags &
             required) == required) {
            hostCoherent = true;
            break;
        }
    }

    const std::string adapterName = context->properties.deviceName;
    return VulkanProbe{
        .context = std::move(context),
        .adapterName = adapterName,
        .deviceLocalBudget = deviceLocalBudget != 0
                                 ? static_cast<std::size_t>(deviceLocalBudget)
                                 : 1024u * 1024u * 1024u,
        .hostCoherent = hostCoherent,
    };
}

[[nodiscard]] Status submit_vulkan_commands(
    const std::shared_ptr<void>& nativeContext,
    std::span<const detail::NativeCommand> commands,
    std::span<const detail::NativeSemaphorePoint> waits,
    std::span<const detail::NativeSemaphorePoint> signals) {
    if (!waits.empty() || !signals.empty()) {
        return Status::failure(
            StatusCode::unsupported,
            "Vulkan timeline semaphore submission is not implemented");
    }
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    if (!context || context->device == VK_NULL_HANDLE ||
        context->queue == VK_NULL_HANDLE) {
        return Status::failure(StatusCode::device_lost,
                               "the Vulkan native context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    return submit_vulkan_command_buffer(*context, commands);
}

} // namespace

Result<Instance> create_vulkan_instance(const InstanceDesc& desc) {
    auto probe = initialize_vulkan(desc);
    if (!probe.ok()) {
        return probe.status();
    }
    auto native = std::move(probe).value();
    detail::FoundationBackendConfig config;
    config.kind = BackendKind::vulkan;
    config.platform = host_platform();
    config.maturity = host_platform() == PlatformKind::linux_host
                          ? BackendMaturity::native_smoke
                          : BackendMaturity::source_only;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics, QueueKind::compute};
    config.supportedFeatures = {Feature::transfer, Feature::memory_budget};
    config.resourceCapabilities = {
        .bufferViews = true,
        .textureViews = true,
        .hostCoherent = native.hostCoherent,
        .bufferCopy = true,
        .bufferFill = true,
        .bufferTextureCopy = true,
        .textureCopy = true,
        .textureClear = true,
        .textureResolve = true,
        .textureBlitNearest = true,
        .textureBlitLinear = true,
        .externalImport = false,
        .externalExport = false,
    };
    const auto& limits = native.context->properties.limits;
    config.bindingCapabilities = {
        .ordinaryBindGroups = true,
        .descriptorArrays = true,
        .dynamicOffsets = true,
        .immutableSamplers = true,
        .pushConstants = true,
        .bindlessTables = false,
        .updateAfterBind = false,
        .maxBindGroups = std::min(limits.maxBoundDescriptorSets, 8u),
        .maxBindingsPerGroup = std::min(limits.maxPerStageResources, 64u),
        .maxDescriptorsPerGroup = std::min(limits.maxPerStageResources, 64u),
        .maxPushConstantBytes = limits.maxPushConstantsSize,
        .minUniformBufferOffsetAlignment = static_cast<std::uint32_t>(
            limits.minUniformBufferOffsetAlignment),
        .minStorageBufferOffsetAlignment = static_cast<std::uint32_t>(
            limits.minStorageBufferOffsetAlignment),
    };
    config.pipelineCapabilities = {
        .graphics = true,
        .compute = true,
        .multipleRenderTargets = false,
        .depthStencil = false,
        .multisample = false,
        .tessellation = false,
        .indirect = false,
        .indirectCount = false,
        .pipelineCache = false,
        .maxColorAttachments = 1,
        .maxVertexBuffers = std::min(limits.maxVertexInputBindings, 16u),
        .maxViewports = 1,
        .maxComputeWorkgroupSize = {limits.maxComputeWorkGroupSize[0],
                                    limits.maxComputeWorkGroupSize[1],
                                    limits.maxComputeWorkGroupSize[2]},
        .maxComputeInvocations = limits.maxComputeWorkGroupInvocations,
    };
    config.deviceLocalBudgetBytes = native.deviceLocalBudget;
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.createBuffer = &create_vulkan_buffer;
    config.mapBuffer = &map_vulkan_buffer;
    config.unmapBuffer = &unmap_vulkan_buffer;
    config.flushBuffer = &flush_vulkan_buffer;
    config.invalidateBuffer = &invalidate_vulkan_buffer;
    config.writeBuffer = &write_vulkan_buffer;
    config.readBuffer = &read_vulkan_buffer;
    config.createTexture = &create_vulkan_texture;
    config.createTextureView = &create_vulkan_texture_view;
    config.writeTexture = &write_vulkan_texture;
    config.readTexture = &read_vulkan_texture;
    config.createSampler = &create_vulkan_sampler;
    config.createShader = &create_vulkan_shader;
    config.createPipeline = &create_vulkan_graphics_pipeline;
    config.createComputePipeline = &create_vulkan_compute_pipeline;
    config.nativeSubmit = &submit_vulkan_commands;
    return detail::create_foundation_instance(desc, std::move(config));
}

} // namespace truffle::rhi
