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
    VolkInstanceTable instanceTable{};
    VolkDeviceTable deviceTable{};
    std::mutex mutex;
};

struct VulkanProbe {
    std::shared_ptr<VulkanContext> context;
    std::string adapterName;
    std::size_t deviceLocalBudget = 1024u * 1024u * 1024u;
};

[[nodiscard]] Status submit_empty_command_buffer(VulkanContext& context) {
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
                              "Vulkan lost the device during smoke submission",
                              result);
    }
    return result == VK_SUCCESS
               ? Status::success()
               : vulkan_failure(StatusCode::backend_error,
                                "Vulkan native smoke submission failed", result);
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

    if (auto status = submit_empty_command_buffer(*context); !status.ok()) {
        return status;
    }

    VkPhysicalDeviceProperties properties{};
    context->instanceTable.vkGetPhysicalDeviceProperties(
        context->physicalDevice, &properties);
    VkPhysicalDeviceMemoryProperties memory{};
    context->instanceTable.vkGetPhysicalDeviceMemoryProperties(
        context->physicalDevice, &memory);
    VkDeviceSize deviceLocalBudget = 0;
    for (std::uint32_t index = 0; index < memory.memoryHeapCount; ++index) {
        if ((memory.memoryHeaps[index].flags &
             VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            deviceLocalBudget += memory.memoryHeaps[index].size;
        }
    }

    return VulkanProbe{
        .context = std::move(context),
        .adapterName = properties.deviceName,
        .deviceLocalBudget = deviceLocalBudget != 0
                                 ? static_cast<std::size_t>(deviceLocalBudget)
                                 : 1024u * 1024u * 1024u,
    };
}

[[nodiscard]] Status submit_vulkan_commands(
    const std::shared_ptr<void>& nativeContext,
    std::span<const detail::NativeCommand> commands,
    std::span<const detail::NativeSemaphorePoint> waits,
    std::span<const detail::NativeSemaphorePoint> signals) {
    if (!commands.empty() || !waits.empty() || !signals.empty()) {
        return Status::failure(
            StatusCode::unsupported,
            "the Vulkan matrix slice currently supports empty native smoke submissions only");
    }
    const auto context = std::static_pointer_cast<VulkanContext>(nativeContext);
    if (!context || context->device == VK_NULL_HANDLE ||
        context->queue == VK_NULL_HANDLE) {
        return Status::failure(StatusCode::device_lost,
                               "the Vulkan native context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    return submit_empty_command_buffer(*context);
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
    config.maturity = host_platform() == PlatformKind::linux
                          ? BackendMaturity::native_smoke
                          : BackendMaturity::source_only;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics};
    config.deviceLocalBudgetBytes = native.deviceLocalBudget;
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.nativeSubmit = &submit_vulkan_commands;
    return detail::create_foundation_instance(desc, std::move(config));
}

} // namespace truffle::rhi
