#include "truffle/rhi/vulkan_backend.hpp"

#include "foundation_backend.hpp"

#include <volk.h>

#include <algorithm>
#include <array>
#include <cstring>
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
                              VkImageView viewValue)
        : texture(std::move(textureValue)), view(viewValue) {}

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
    const auto usage = vulkan_texture_usage(desc.usage);
    if (usage == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "Vulkan texture usage is empty");
    }
    VkFormatProperties properties{};
    context->instanceTable.vkGetPhysicalDeviceFormatProperties(
        context->physicalDevice, format.format, &properties);
    const auto requiredFeatures = required_format_features(desc.usage);
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
            std::make_shared<VulkanTextureViewResource>(texture, view));
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
            return Status::failure(
                StatusCode::unsupported,
                "the Vulkan resource slice supports transfer command lists only");
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
        commandBuffer, sourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
        nullptr, 0, nullptr, 1, &barrier);
    oldLayout = newLayout;
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
    std::span<const detail::NativeCommand> commands) {
    bool recordedTransfer = false;
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
            return Status::failure(
                StatusCode::unsupported,
                "the Vulkan resource slice supports transfer command lists only");
        }
        const auto& transfer = command.transfer;
        if (recordedTransfer) {
            memory_barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_TRANSFER_WRITE_BIT);
        } else {
            memory_barrier(VK_PIPELINE_STAGE_HOST_BIT,
                           VK_ACCESS_HOST_WRITE_BIT,
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
        if (auto status = record_vulkan_commands(context, commandBuffer,
                                                 commands);
            !status.ok()) {
            context.deviceTable.vkDestroyCommandPool(context.device, pool,
                                                      nullptr);
            return status;
        }
    }
    if (result == VK_SUCCESS) {
        result = context.deviceTable.vkEndCommandBuffer(commandBuffer);
    }
    if (result == VK_SUCCESS) {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = context.deviceTable.vkQueueSubmit(context.queue, 1, &submitInfo,
                                                   VK_NULL_HANDLE);
    }
    if (result == VK_SUCCESS) {
        result = context.deviceTable.vkQueueWaitIdle(context.queue);
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
    const auto imageStride = dataRow * rowsPerImage;
    const auto required = dataLayout.offset +
                          (region.extent.depth - 1u) * imageStride +
                          (blocksHigh - 1u) * dataRow + tightRow;
    if (required < dataLayout.offset || required > dataSize) {
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
    const auto nativeDepthStride =
        nativeLayout.depthPitch == 0
            ? nativeLayout.rowPitch *
                  divide_round_up(
                      mip_dimension(texture.desc.extent.height,
                                    region.subresource.mipLevel),
                      texture.format.blockHeight)
            : nativeLayout.depthPitch;
    const auto nativeOffset = nativeLayout.offset +
                              static_cast<VkDeviceSize>(region.origin.z) *
                                  nativeDepthStride +
                              static_cast<VkDeviceSize>(blockY) *
                                  nativeLayout.rowPitch +
                              static_cast<VkDeviceSize>(blockX) *
                                  texture.format.bytesPerBlock;
    const auto nativeRequired = nativeOffset +
                                static_cast<VkDeviceSize>(region.extent.depth - 1u) *
                                    nativeDepthStride +
                                static_cast<VkDeviceSize>(blocksHigh - 1u) *
                                    nativeLayout.rowPitch +
                                tightRow;
    if (nativeRequired > texture.allocationSize) {
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
    config.queueKinds = {QueueKind::graphics};
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
    config.nativeSubmit = &submit_vulkan_commands;
    return detail::create_foundation_instance(desc, std::move(config));
}

} // namespace truffle::rhi
