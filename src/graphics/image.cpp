#include "graphics/image.hpp"
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
void check(VkResult r) { if (r != VK_SUCCESS) throw std::runtime_error("Image Vulkan call failed: " + std::to_string(r)); }
uint32_t memory_type(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No suitable Vulkan memory type for scene resources");
}

}
void gpu::create_image(gpu::Image& image, VkPhysicalDevice physical, VkDevice device,
                  VkExtent3D extent, VkImageType type, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    VkImageFormatProperties supported{};
    check(vkGetPhysicalDeviceImageFormatProperties(physical, format, type,
        VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported));
    if (!extent.width || !extent.height || !extent.depth || extent.width > supported.maxExtent.width ||
        extent.height > supported.maxExtent.height || extent.depth > supported.maxExtent.depth)
        throw std::invalid_argument("Image extent exceeds Vulkan limits");
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = type;
    info.format = format;
    info.extent = extent;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    check(vkCreateImage(device, &info, nullptr, &image.image));
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &allocation, nullptr, &image.memory));
    check(vkBindImageMemory(device, image.image, image.memory, 0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    check(vkCreateImageView(device, &view, nullptr, &image.view));
}

void gpu::destroy_image(gpu::Image& image, VkDevice device) {
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = {};
}

void gpu::upload_image(const Image& image, VkPhysicalDevice physical, VkDevice device,
                       VkQueue queue, uint32_t queue_family, VkExtent3D extent,
                       const void* data, size_t bytes, VkImageLayout final_layout) {
    VkMemoryRequirements requirements{};
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkBuffer staging{};
    VkDeviceMemory staging_memory{};
    VkCommandPool pool{};
    VkFence fence{};
    bool submitted = false;
    auto cleanup = [&] {
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (staging) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory) vkFreeMemory(device, staging_memory, nullptr);
    };
    try {
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = bytes;
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        check(vkCreateBuffer(device, &buffer, nullptr, &staging));
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &staging_memory));
        check(vkBindBufferMemory(device, staging, staging_memory, 0));
        void* mapped{};
        check(vkMapMemory(device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        std::memcpy(mapped, data, static_cast<size_t>(buffer.size));
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = staging_memory;
        range.size = VK_WHOLE_SIZE;
        const VkResult flushed = vkFlushMappedMemoryRanges(device, 1, &range);
        vkUnmapMemory(device, staging_memory);
        check(flushed);
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = queue_family;
        check(vkCreateCommandPool(device, &pool_info, nullptr, &pool));
        VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commands.commandPool = pool;
        commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commands.commandBufferCount = 1;
        VkCommandBuffer command{};
        check(vkAllocateCommandBuffers(device, &commands, &command));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin));
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = extent;
        vkCmdCopyBufferToImage(command, staging, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = final_layout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
            (final_layout == VK_IMAGE_LAYOUT_GENERAL ? VK_ACCESS_SHADER_WRITE_BIT : 0);
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        check(vkEndCommandBuffer(command));
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device, &fence_info, nullptr, &fence));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, fence));
        submitted = true;
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    } catch (...) {
        if (submitted) vkDeviceWaitIdle(device);
        cleanup();
        throw;
    }
    cleanup();

}
