#include "renderer/vulkan_backend.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}

void vk_check(VkResult result) {
    if (result != VK_SUCCESS)
        fail("A Vulkan call failed");
}

struct Frame {
    VkCommandPool pool{};
    VkCommandBuffer command_buffer{};
    VkFence fence{};
    VkImageView view{};
    VkFramebuffer framebuffer{};
};

struct VulkanContext {
    VkInstance instance{};
    VkPhysicalDevice physical_device{};
    VkDevice device{};
    uint32_t queue_family = UINT32_MAX;
    VkQueue queue{};
    VkSurfaceKHR surface{};
    VkSwapchainKHR swapchain{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkRenderPass render_pass{};
    VkDescriptorPool descriptor_pool{};
    std::vector<VkImage> images;
    std::vector<Frame> frames;
    VkSemaphore image_acquired{};
    VkSemaphore render_complete{};

    void create(SDL_Window* window) {
        Uint32 extension_count = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (!extensions)
            fail(SDL_GetError());

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Pomelo";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "none";
        app.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app;
        instance_info.enabledExtensionCount = extension_count;
        instance_info.ppEnabledExtensionNames = extensions;
        vk_check(vkCreateInstance(&instance_info, nullptr, &instance));

        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
            fail(SDL_GetError());

        uint32_t device_count = 0;
        vk_check(vkEnumeratePhysicalDevices(instance, &device_count, nullptr));
        if (device_count == 0)
            fail("No Vulkan-capable GPU was found");
        std::vector<VkPhysicalDevice> devices(device_count);
        vk_check(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()));

        for (VkPhysicalDevice candidate : devices) {
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> properties(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, properties.data());
            for (uint32_t i = 0; i < count; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
                if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    physical_device = candidate;
                    queue_family = i;
                    break;
                }
            }
            if (physical_device)
                break;
        }
        if (!physical_device)
            fail("No graphics/present Vulkan queue was found");

        constexpr float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = 1;
        device_info.ppEnabledExtensionNames = device_extensions;
        vk_check(vkCreateDevice(physical_device, &device_info, nullptr, &device));
        vkGetDeviceQueue(device, queue_family, 0, &queue);

        std::array<VkDescriptorPoolSize, 1> pool_sizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
        }};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1024;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        vk_check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool));

        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vk_check(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_acquired));
        vk_check(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_complete));
        rebuild_swapchain(window);
    }

    void destroy_swapchain() {
        for (auto& frame : frames) {
            if (frame.framebuffer) vkDestroyFramebuffer(device, frame.framebuffer, nullptr);
            if (frame.view) vkDestroyImageView(device, frame.view, nullptr);
            if (frame.fence) vkDestroyFence(device, frame.fence, nullptr);
            if (frame.pool) vkDestroyCommandPool(device, frame.pool, nullptr);
        }
        frames.clear();
        images.clear();
        if (render_pass) vkDestroyRenderPass(device, render_pass, nullptr);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        render_pass = VK_NULL_HANDLE;
        swapchain = VK_NULL_HANDLE;
    }

    void rebuild_swapchain(SDL_Window* window) {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width <= 0 || height <= 0)
            return;

        vkDeviceWaitIdle(device);
        destroy_swapchain();

        VkSurfaceCapabilitiesKHR capabilities{};
        vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities));
        uint32_t format_count = 0;
        vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr));
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data()));
        if (formats.empty()) fail("The Vulkan surface has no supported formats");
        auto selected = formats.front();
        for (const auto& candidate : formats) {
            if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_B8G8R8A8_SRGB) {
                selected = candidate;
                break;
            }
        }
        format = selected.format;
        extent = capabilities.currentExtent.width != UINT32_MAX
            ? capabilities.currentExtent
            : VkExtent2D{std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                         std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
        uint32_t image_count = std::max(2u, capabilities.minImageCount);
        if (capabilities.maxImageCount != 0)
            image_count = std::min(image_count, capabilities.maxImageCount);

        VkSwapchainCreateInfoKHR swapchain_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swapchain_info.surface = surface;
        swapchain_info.minImageCount = image_count;
        swapchain_info.imageFormat = selected.format;
        swapchain_info.imageColorSpace = selected.colorSpace;
        swapchain_info.imageExtent = extent;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.preTransform = capabilities.currentTransform;
        swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_info.clipped = VK_TRUE;
        vk_check(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain));

        vk_check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr));
        images.resize(image_count);
        vk_check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data()));

        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;
        vk_check(vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass));

        frames.resize(images.size());
        for (size_t i = 0; i < frames.size(); ++i) {
            auto& frame = frames[i];
            VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view_info.image = images[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            vk_check(vkCreateImageView(device, &view_info, nullptr, &frame.view));
            VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer_info.renderPass = render_pass;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = &frame.view;
            framebuffer_info.width = extent.width;
            framebuffer_info.height = extent.height;
            framebuffer_info.layers = 1;
            vk_check(vkCreateFramebuffer(device, &framebuffer_info, nullptr, &frame.framebuffer));
            VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            command_pool_info.queueFamilyIndex = queue_family;
            vk_check(vkCreateCommandPool(device, &command_pool_info, nullptr, &frame.pool));
            VkCommandBufferAllocateInfo allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate_info.commandPool = frame.pool;
            allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount = 1;
            vk_check(vkAllocateCommandBuffers(device, &allocate_info, &frame.command_buffer));
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vk_check(vkCreateFence(device, &fence_info, nullptr, &frame.fence));
        }
    }

    graphics::Frame begin_frame(SDL_Window* window) {
        if (!swapchain || extent.width == 0 || extent.height == 0)
            return {};
        uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_acquired, VK_NULL_HANDLE, &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { rebuild_swapchain(window); return {}; }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) vk_check(acquire);
        Frame& frame = frames[image_index];
        vk_check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        vk_check(vkResetFences(device, 1, &frame.fence));
        vk_check(vkResetCommandPool(device, frame.pool, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk_check(vkBeginCommandBuffer(frame.command_buffer, &begin));
        VkClearValue clear{{{0.035f, 0.04f, 0.05f, 1.0f}}};
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = render_pass;
        pass.framebuffer = frame.framebuffer;
        pass.renderArea.extent = extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(frame.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
        return {frame.command_buffer, image_index, true};
    }

    void end_frame(graphics::Frame token, SDL_Window* window) {
        Frame& frame = frames[token.image_index];
        const uint32_t image_index = token.image_index;
        vkCmdEndRenderPass(frame.command_buffer);
        vk_check(vkEndCommandBuffer(frame.command_buffer));
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_acquired;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.command_buffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_complete;
        vk_check(vkQueueSubmit(queue, 1, &submit, frame.fence));
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_complete;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image_index;
        VkResult result = vkQueuePresentKHR(queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) rebuild_swapchain(window);
        else vk_check(result);
    }

    void destroy() {
        if (!device) return;
        vkDeviceWaitIdle(device);
        destroy_swapchain();
        if (image_acquired) vkDestroySemaphore(device, image_acquired, nullptr);
        if (render_complete) vkDestroySemaphore(device, render_complete, nullptr);
        if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        vkDestroyDevice(device, nullptr);
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};

} // namespace

struct graphics::VulkanBackend::Impl {
    SDL_Window* window{};
    VulkanContext vk;
};

graphics::VulkanBackend::VulkanBackend(void* native_window) : impl_(std::make_unique<Impl>()) {
    impl_->window = static_cast<SDL_Window*>(native_window);
    impl_->vk.create(impl_->window);

    const BackendInfo backend = backend_info();
    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = VK_API_VERSION_1_2;
    init.Instance = static_cast<VkInstance>(backend.instance);
    init.PhysicalDevice = static_cast<VkPhysicalDevice>(backend.physical_device);
    init.Device = static_cast<VkDevice>(backend.device);
    init.QueueFamily = backend.queue_family;
    init.Queue = static_cast<VkQueue>(backend.queue);
    init.DescriptorPool = static_cast<VkDescriptorPool>(backend.descriptor_pool);
    init.MinImageCount = 2;
    init.ImageCount = backend.image_count;
    init.PipelineInfoMain.RenderPass = static_cast<VkRenderPass>(backend.render_pass);
    init.PipelineInfoMain.Subpass = 0;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.CheckVkResultFn = vk_check;
    if (!ImGui_ImplVulkan_Init(&init))
        fail("Could not initialize ImGui Vulkan backend");
}

graphics::VulkanBackend::~VulkanBackend() {
    if (impl_->vk.device) vkDeviceWaitIdle(impl_->vk.device);
    ImGui_ImplVulkan_Shutdown();
    impl_->vk.destroy();
}
graphics::VulkanBackend::VulkanBackend(VulkanBackend&&) noexcept = default;
graphics::VulkanBackend& graphics::VulkanBackend::operator=(VulkanBackend&&) noexcept = default;

graphics::BackendInfo graphics::VulkanBackend::backend_info() const {
    const auto& vk = impl_->vk;
    return {vk.instance, vk.physical_device, vk.device, vk.queue, vk.render_pass,
            vk.descriptor_pool, vk.queue_family, static_cast<std::uint32_t>(vk.images.size())};
}

graphics::Frame graphics::VulkanBackend::begin_frame() {
    return impl_->vk.begin_frame(impl_->window);
}
void graphics::VulkanBackend::begin_imgui_frame() { ImGui_ImplVulkan_NewFrame(); }
void graphics::VulkanBackend::render_imgui(void* draw_data, void* command_buffer) {
    ImGui_ImplVulkan_RenderDrawData(static_cast<ImDrawData*>(draw_data),
                                   static_cast<VkCommandBuffer>(command_buffer));
}
void graphics::VulkanBackend::end_frame(Frame frame) { impl_->vk.end_frame(frame, impl_->window); }
void graphics::VulkanBackend::wait_idle() { vkDeviceWaitIdle(impl_->vk.device); }
