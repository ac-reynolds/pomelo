#include "graphics/vulkan_backend.hpp"
#include "graphics/volume_view.hpp"
#include "graphics/advection.hpp"
#include "graphics/divergence.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}

void vk_check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("Vulkan call failed: " + std::to_string(result));
}

struct Frame {
    VkCommandPool pool{};
    VkCommandBuffer command_buffer{};
    VkFence fence{};
    VkSemaphore image_acquired{};
    graphics::RenderTarget target;
    VkDescriptorSet texture{};
};

struct PresentImage {
    VkImageView view{};
    VkFramebuffer framebuffer{};
    // Presentation consumes this semaphore before this image can be reacquired.
    VkSemaphore render_complete{};
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
    std::vector<PresentImage> present_images;
    uint32_t frame_index{};
    graphics::VolumePipeline volume_pipeline;
    graphics::VolumeView volume_view;
    std::array<graphics::VolumeView, 2> density_views;
    simulation::Divergence divergence;
    simulation::SimulationFields fields;
    simulation::AdvectionStep pending_step;
    simulation::Advection simulation;
    bool step_recorded{};
    bool imgui_initialized{};
    bool swapchain_dirty{};

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
                if ((properties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) && present) {
                    physical_device = candidate;
                    queue_family = i;
                    break;
                }
            }
            if (physical_device)
                break;
        }
        if (!physical_device)
            fail("No graphics/compute/present Vulkan queue was found");

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

        std::array<VkDescriptorPoolSize, 2> pool_sizes{{
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
            {VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
        }};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1024 + IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        vk_check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool));

        rebuild_swapchain(window);
    }

    void destroy_swapchain() {
        for (auto& frame : frames) {
            if (frame.texture) ImGui_ImplVulkan_RemoveTexture(frame.texture);
            graphics::destroy_render_target(frame.target, device);
            if (frame.image_acquired) vkDestroySemaphore(device, frame.image_acquired, nullptr);
            if (frame.fence) vkDestroyFence(device, frame.fence, nullptr);
            if (frame.pool) vkDestroyCommandPool(device, frame.pool, nullptr);
        }
        frames.clear();
        frame_index = 0;
        for (auto& image : present_images) {
            if (image.framebuffer) vkDestroyFramebuffer(device, image.framebuffer, nullptr);
            if (image.view) vkDestroyImageView(device, image.view, nullptr);
            if (image.render_complete) vkDestroySemaphore(device, image.render_complete, nullptr);
        }
        present_images.clear();
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

        vk_check(vkDeviceWaitIdle(device));
        const bool restart_imgui = imgui_initialized;
        destroy_swapchain();
        if (restart_imgui) {
            ImGui_ImplVulkan_Shutdown();
            imgui_initialized = false;
        }

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

        present_images.resize(images.size());
        for (size_t i = 0; i < present_images.size(); ++i) {
            auto& frame = present_images[i];
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vk_check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.render_complete));
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
        }
        frames.resize(2);
        for (auto& frame : frames) {
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vk_check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.image_acquired));
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
        if (restart_imgui) initialize_imgui();
    }

    void initialize_imgui() {
        ImGui_ImplVulkan_InitInfo init{};
        init.ApiVersion = VK_API_VERSION_1_2;
        init.Instance = instance;
        init.PhysicalDevice = physical_device;
        init.Device = device;
        init.QueueFamily = queue_family;
        init.Queue = queue;
        init.DescriptorPool = descriptor_pool;
        init.MinImageCount = 2;
        init.ImageCount = static_cast<uint32_t>(images.size());
        init.PipelineInfoMain.RenderPass = render_pass;
        init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init.CheckVkResultFn = vk_check;
        if (!ImGui_ImplVulkan_Init(&init)) fail("Could not initialize ImGui Vulkan backend");
        imgui_initialized = true;
    }

    graphics::Frame begin_frame(SDL_Window* window) {
        if (swapchain_dirty || !swapchain) {
            rebuild_swapchain(window);
            swapchain_dirty = false;
        }
        if (!swapchain || extent.width == 0 || extent.height == 0) return {};
        Frame& frame = frames[frame_index];
        vk_check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                frame.image_acquired, VK_NULL_HANDLE, &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { swapchain_dirty = true; return {}; }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) vk_check(acquire);
        swapchain_dirty = acquire == VK_SUBOPTIMAL_KHR;
        vk_check(vkResetCommandPool(device, frame.pool, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk_check(vkBeginCommandBuffer(frame.command_buffer, &begin));
        return {frame.command_buffer, image_index, frame_index, true};
    }

    void begin_presentation(graphics::Frame token) {
        VkClearValue clear{{{0.035f, 0.04f, 0.05f, 1.0f}}};
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = render_pass;
        pass.framebuffer = present_images[token.image_index].framebuffer;
        pass.renderArea.extent = extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(frames[token.frame_index].command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    }

    void end_frame(graphics::Frame token) {
        Frame& frame = frames[token.frame_index];
        auto& image = present_images[token.image_index];
        const uint32_t image_index = token.image_index;
        vkCmdEndRenderPass(frame.command_buffer);
        vk_check(vkEndCommandBuffer(frame.command_buffer));
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.image_acquired;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.command_buffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &image.render_complete;
        vk_check(vkResetFences(device, 1, &frame.fence));
        vk_check(vkQueueSubmit(queue, 1, &submit, frame.fence));
        if (step_recorded) simulation::commit_step(fields, pending_step);
        step_recorded = false;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &image.render_complete;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image_index;
        VkResult result = vkQueuePresentKHR(queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) swapchain_dirty = true;
        else vk_check(result);
        frame_index = (frame_index + 1) % static_cast<uint32_t>(frames.size());
    }

    void destroy() {
        if (device) {
            vkDeviceWaitIdle(device);
            destroy_swapchain();
            graphics::destroy_volume_view(volume_view, device);
            for (auto& view : density_views) graphics::destroy_volume_view(view, device);
            simulation::destroy_divergence(divergence, device);
            graphics::destroy_volume_pipeline(volume_pipeline, device);
            simulation::destroy_advection(simulation, device);
            simulation::destroy_fields(fields, device);
            if (imgui_initialized) ImGui_ImplVulkan_Shutdown();
            if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};

} // namespace

struct graphics::VulkanBackend::Impl {
    SDL_Window* window{};
    VulkanContext vk;
};

graphics::VulkanBackend::VulkanBackend(void* native_window, const simulation::InitialConditions& initial) : impl_(std::make_unique<Impl>()) {
    impl_->window = static_cast<SDL_Window*>(native_window);
    try {
        impl_->vk.create(impl_->window);
        impl_->vk.initialize_imgui();
        auto& vk = impl_->vk;
        simulation::create_fields(vk.fields, vk.physical_device, vk.device, vk.queue, vk.queue_family, initial);
        const std::array fields{simulation::density_view(vk.fields, 0, 0), simulation::density_view(vk.fields, 1, 0)};
        simulation::create_advection(vk.simulation, vk.physical_device, vk.device, fields, {simulation::velocity_view(vk.fields, 0, 0), simulation::velocity_view(vk.fields, 1, 0)});
        create_volume_pipeline(vk.volume_pipeline, vk.physical_device, vk.device);
        const auto divergence = simulation::divergence_view(vk.fields, 0);
        simulation::create_divergence(vk.divergence, vk.physical_device, vk.device,
            {simulation::velocity_view(vk.fields, 0, 0), simulation::velocity_view(vk.fields, 1, 0)}, divergence);
        create_volume_view(vk.volume_view, vk.device, vk.volume_pipeline, divergence, true);
        for (size_t i = 0; i < fields.size(); ++i)
            create_volume_view(vk.density_views[i], vk.device, vk.volume_pipeline, fields[i]);
    } catch (...) {
        impl_->vk.destroy();
        throw;
    }
}

graphics::VulkanBackend::~VulkanBackend() { impl_->vk.destroy(); }
graphics::BackendInfo graphics::VulkanBackend::backend_info() const {
    const auto& vk = impl_->vk;
    return {vk.instance, vk.physical_device, vk.device, vk.queue, vk.render_pass,
            vk.descriptor_pool, vk.queue_family, static_cast<std::uint32_t>(vk.images.size())};
}

graphics::Frame graphics::VulkanBackend::begin_frame() {
    return impl_->vk.begin_frame(impl_->window);
}
void graphics::VulkanBackend::begin_imgui_frame() { ImGui_ImplVulkan_NewFrame(); }
void graphics::VulkanBackend::render_imgui(void* draw_data, Frame frame) {
    impl_->vk.begin_presentation(frame);
    ImGui_ImplVulkan_RenderDrawData(static_cast<ImDrawData*>(draw_data),
                                   static_cast<VkCommandBuffer>(frame.command_buffer));
}
void graphics::VulkanBackend::end_frame(Frame frame) { impl_->vk.end_frame(frame); }
void graphics::VulkanBackend::wait_idle() { vkDeviceWaitIdle(impl_->vk.device); }

std::uint64_t graphics::VulkanBackend::prepare_viewport(Frame token, int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    auto& vk = impl_->vk;
    auto& frame = vk.frames[token.frame_index];
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vk.physical_device, &properties);
    const VkExtent2D extent{
        std::min(static_cast<uint32_t>(width), std::min(properties.limits.maxImageDimension2D, properties.limits.maxFramebufferWidth)),
        std::min(static_cast<uint32_t>(height), std::min(properties.limits.maxImageDimension2D, properties.limits.maxFramebufferHeight))};
    if (extent.width != frame.target.extent.width || extent.height != frame.target.extent.height) {
        if (frame.texture) ImGui_ImplVulkan_RemoveTexture(frame.texture);
        frame.texture = VK_NULL_HANDLE;
        destroy_render_target(frame.target, vk.device);
        create_render_target(frame.target, {vk.volume_pipeline.render_pass, vk.volume_pipeline.color_format, vk.volume_pipeline.depth_format}, vk.physical_device, vk.device, extent);
        frame.texture = ImGui_ImplVulkan_AddTexture(frame.target.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return reinterpret_cast<std::uintptr_t>(frame.texture);
}

graphics::SimulationOutput graphics::VulkanBackend::step_simulation(Frame token) {
    auto& vk = impl_->vk;
    if (vk.step_recorded) throw std::logic_error("Simulation already recorded for this frame");
    vk.pending_step = simulation::prepare_advection(vk.fields);
    simulation::record_advection(vk.simulation, vk.frames[token.frame_index].command_buffer, vk.pending_step);
    simulation::record_divergence(vk.divergence, vk.frames[token.frame_index].command_buffer, vk.pending_step.velocity_output);
    vk.step_recorded = true;
    return {vk.pending_step.output, simulation::divergence_view(vk.fields, vk.pending_step.output.version)};
}

void graphics::VulkanBackend::render_volume(Frame token, const FieldView& field, const OrbitCamera& camera) {
    const auto& vk = impl_->vk;
    const auto& frame = vk.frames[token.frame_index];
    if (!frame.target.framebuffer) return;
    if (same_field_binding(vk.volume_view.field, field)) {
        record_volume_view(vk.volume_view, frame.target, frame.command_buffer, camera);
        return;
    }
    for (const auto& view : vk.density_views) {
        if (same_field_binding(view.field, field)) {
            record_volume_view(view, frame.target, frame.command_buffer, camera);
            return;
        }
    }
    throw std::invalid_argument("No volume view configured for the selected field");
}
