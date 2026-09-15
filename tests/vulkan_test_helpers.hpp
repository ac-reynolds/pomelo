#pragma once
#include "graphics/advection.hpp"
#include "graphics/divergence.hpp"
#include "graphics/volume_view.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
void check(VkResult r) { if (r != VK_SUCCESS) throw std::runtime_error("Test Vulkan error " + std::to_string(r)); }
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Slot { VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{}; graphics::RenderTarget target; };
struct Context {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    std::array<Slot, 2> slots;
    VkDebugUtilsMessengerEXT messenger{};
    unsigned errors{};
};
VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++static_cast<Context*>(user)->errors;
        std::fprintf(stderr, "%s\n", data->pMessage);
    }
    return VK_FALSE;
}
bool create(Context& c) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    uint32_t count{};
    check(vkEnumerateInstanceLayerProperties(&count, nullptr));
    std::vector<VkLayerProperties> layers(count);
    check(vkEnumerateInstanceLayerProperties(&count, layers.data()));
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const bool validation = std::any_of(layers.begin(), layers.end(), [&](auto& p) { return std::strcmp(p.layerName, layer) == 0; });
    VkValidationFeatureEnableEXT feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = &feature;
    const char* extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
    if (validation) {
        info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer;
        info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = extensions;
        info.pNext = &features;
    }
    check(vkCreateInstance(&info, nullptr, &c.instance));
    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        messenger.pfnUserCallback = debug;
        messenger.pUserData = &c;
        check(reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(c.instance,
            "vkCreateDebugUtilsMessengerEXT"))(c.instance, &messenger, nullptr, &c.messenger));
    }
    std::printf("Synchronization validation: %s\n", validation ? "enabled" : "unavailable");
    check(vkEnumeratePhysicalDevices(c.instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(c.instance, &count, devices.data()));
    for (auto device : devices) {
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
        for (uint32_t i = 0; i < count; ++i)
            if ((families[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) ==
                (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) { c.physical = device; c.family = i; break; }
        if (c.physical) break;
    }
    if (!c.physical) return false;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(c.physical, &properties);
    std::printf("GPU: %s\n", properties.deviceName);
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = c.family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
    check(vkCreateDevice(c.physical, &device, nullptr, &c.device));
    vkGetDeviceQueue(c.device, c.family, 0, &c.queue);
    for (auto& slot : c.slots) {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = c.family;
        check(vkCreateCommandPool(c.device, &pool, nullptr, &slot.pool));
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = slot.pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(c.device, &allocate, &slot.command));
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(c.device, &fence, nullptr, &slot.fence));
    }
    return true;
}
void destroy(Context& c) {
    if (c.device) {
        vkDeviceWaitIdle(c.device);
        for (auto& slot : c.slots) {
            graphics::destroy_render_target(slot.target, c.device);
            if (slot.fence) vkDestroyFence(c.device, slot.fence, nullptr);
            if (slot.pool) vkDestroyCommandPool(c.device, slot.pool, nullptr);
        }
        vkDestroyDevice(c.device, nullptr);
    }
    if (c.messenger) reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(c.instance,
        "vkDestroyDebugUtilsMessengerEXT"))(c.instance, c.messenger, nullptr);
    if (c.instance) vkDestroyInstance(c.instance, nullptr);
    const unsigned errors = c.errors;
    c = {};
    c.errors = errors;
}
void begin(Context& c, Slot& slot) {
    check(vkWaitForFences(c.device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
    check(vkResetCommandPool(c.device, slot.pool, 0));
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(slot.command, &info));
}
void submit(Context& c, Slot& slot) {
    check(vkEndCommandBuffer(slot.command));
    check(vkResetFences(c.device, 1, &slot.fence));
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO}; info.commandBufferCount = 1; info.pCommandBuffers = &slot.command;
    check(vkQueueSubmit(c.queue, 1, &info, slot.fence));
}
std::vector<float> read_image(Context& c, const gpu::Image& image, VkExtent3D extent,
                              size_t components, VkImageLayout layout) {
    const size_t bytes = size_t(extent.width) * extent.height * extent.depth * components * sizeof(float);
    VkBuffer buffer{}; VkDeviceMemory memory{};
    auto cleanup = [&] {
        if (buffer) vkDestroyBuffer(c.device, buffer, nullptr);
        if (memory) vkFreeMemory(c.device, memory, nullptr);
    };
    try {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = bytes; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(c.device, &info, nullptr, &buffer));
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(c.device, buffer, &requirements);
        VkPhysicalDeviceMemoryProperties properties{}; vkGetPhysicalDeviceMemoryProperties(c.physical, &properties);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { type = i; break; }
        require(type != UINT32_MAX, "No readback memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
        check(vkAllocateMemory(c.device, &allocation, nullptr, &memory));
        check(vkBindBufferMemory(c.device, buffer, memory, 0));
        auto& slot = c.slots[0]; begin(c, slot);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = image.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = layout; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = extent;
        vkCmdCopyImageToBuffer(slot.command, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
        std::swap(barrier.oldLayout, barrier.newLayout);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host, 0, nullptr, 0, nullptr);
        submit(c, slot);
        check(vkWaitForFences(c.device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
        void* mapped{}; check(vkMapMemory(c.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = memory; range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(c.device, 1, &range));
        std::vector<float> result(bytes / sizeof(float)); std::memcpy(result.data(), mapped, bytes);
        vkUnmapMemory(c.device, memory);
        cleanup(); return result;
    } catch (...) { vkDeviceWaitIdle(c.device); cleanup(); throw; }
}
}
