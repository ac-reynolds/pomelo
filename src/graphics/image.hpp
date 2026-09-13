#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>

namespace gpu {
struct Image { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; };
// The caller owns lifetime and waits for outstanding GPU accesses before destruction.
void create_image(Image&, VkPhysicalDevice, VkDevice, VkExtent3D, VkImageType,
                  VkFormat, VkImageUsageFlags, VkImageAspectFlags);
void destroy_image(Image&, VkDevice);
// Initialization only: uploads and waits for completion. Image starts UNDEFINED.
void upload_image(const Image&, VkPhysicalDevice, VkDevice, VkQueue, uint32_t queue_family,
                  VkExtent3D, const void*, size_t bytes, VkImageLayout final_layout);
}
