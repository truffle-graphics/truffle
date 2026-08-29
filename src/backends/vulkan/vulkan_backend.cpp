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
        case detail::NativeTransferKind::copy_buffer_to_texture:
        case detail::NativeTransferKind::copy_texture_to_buffer:
        case detail::NativeTransferKind::copy_texture:
        case detail::NativeTransferKind::clear_texture:
        case detail::NativeTransferKind::resolve_texture:
        case detail::NativeTransferKind::blit_texture:
            return Status::failure(
                StatusCode::unsupported,
                "Vulkan texture transfers are not implemented in this resource slice");
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
        .pEnabledFeatures = nullptr,
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
        .textureViews = false,
        .hostCoherent = native.hostCoherent,
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
    config.createBuffer = &create_vulkan_buffer;
    config.mapBuffer = &map_vulkan_buffer;
    config.unmapBuffer = &unmap_vulkan_buffer;
    config.flushBuffer = &flush_vulkan_buffer;
    config.invalidateBuffer = &invalidate_vulkan_buffer;
    config.writeBuffer = &write_vulkan_buffer;
    config.readBuffer = &read_vulkan_buffer;
    config.nativeSubmit = &submit_vulkan_commands;
    return detail::create_foundation_instance(desc, std::move(config));
}

} // namespace truffle::rhi
