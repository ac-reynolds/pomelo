#include "graphics/vulkan_backend.hpp"
#include "graphics/volume_view.hpp"
#include "graphics/advection.hpp"
#include "graphics/smoke.hpp"
#include "graphics/divergence.hpp"
#include "graphics/pressure.hpp"
#include "graphics/projection.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

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
};

struct PresentImage {
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
    std::vector<VkImage> images;
    std::vector<Frame> frames;
    std::vector<PresentImage> present_images;
    uint32_t frame_index{};
    graphics::VolumePipeline volume_pipeline;
    std::array<graphics::VolumeView, 2> pressure_views;
    simulation::PressureSolve pressure;
    simulation::Projection projection;
    std::array<bool, 2> pressure_submitted{};
    std::string pressure_status{"Ready"};
    std::array<graphics::VolumeView, 2> density_views;
    simulation::Divergence divergence;
    simulation::SimulationFields fields;
    simulation::AdvectionStep pending_step;
    simulation::Advection simulation;
    simulation::Smoke smoke;
    simulation::SmokeParameters smoke_parameters;
    std::vector<std::array<float, 3>> sources;
    bool step_recorded{};
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

        rebuild_swapchain(window);
    }

    void destroy_swapchain() {
        for (auto& frame : frames) {
            graphics::destroy_render_target(frame.target, device);
            if (frame.image_acquired) vkDestroySemaphore(device, frame.image_acquired, nullptr);
            if (frame.fence) vkDestroyFence(device, frame.fence, nullptr);
            if (frame.pool) vkDestroyCommandPool(device, frame.pool, nullptr);
        }
        frames.clear();
        frame_index = 0;
        for (auto& image : present_images) {
            if (image.render_complete) vkDestroySemaphore(device, image.render_complete, nullptr);
        }
        present_images.clear();
        images.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    void rebuild_swapchain(SDL_Window* window) {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width <= 0 || height <= 0)
            return;

        vk_check(vkDeviceWaitIdle(device));
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
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            fail("The Vulkan surface does not support transfer presentation");
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &format_properties);
        if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            fail("The Vulkan surface format does not support blitting");
        swapchain_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.preTransform = capabilities.currentTransform;
        swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_info.clipped = VK_TRUE;
        vk_check(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain));

        vk_check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr));
        images.resize(image_count);
        vk_check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data()));

        present_images.resize(images.size());
        for (size_t i = 0; i < present_images.size(); ++i) {
            auto& frame = present_images[i];
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vk_check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.render_complete));
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
    }

    graphics::Frame begin_frame(SDL_Window* window) {
        if (swapchain_dirty || !swapchain) {
            rebuild_swapchain(window);
            swapchain_dirty = false;
        }
        if (!swapchain || extent.width == 0 || extent.height == 0) return {};
        Frame& frame = frames[frame_index];
        vk_check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        if (pressure_submitted[frame_index]) {
            const auto diagnostics = simulation::read_pressure_diagnostics(pressure, device, frame_index);
            if (!diagnostics.finite) pressure_status = "Pressure solve produced nonfinite values";
            else if (!simulation::pressure_converged(diagnostics)) {
                pressure_status = "Pressure iteration budget exhausted: residual " + std::to_string(diagnostics.residual_rms)
                    + ", RHS " + std::to_string(diagnostics.rhs_rms);
            } else pressure_status = "Ready";
            pressure_submitted[frame_index] = false;
        }
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

    void end_frame(graphics::Frame token) {
        Frame& frame = frames[token.frame_index];
        auto& image = present_images[token.image_index];
        const uint32_t image_index = token.image_index;
        VkImageMemoryBarrier barriers[2]{};
        for (auto& barrier : barriers) {
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }
        barriers[0].image = frame.target.color.image;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[1].image = images[image_index];
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(frame.command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        blit.dstOffsets[1] = blit.srcOffsets[1];
        vkCmdBlitImage(frame.command_buffer, frame.target.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].dstAccessMask = 0;
        vkCmdPipelineBarrier(frame.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barriers[1]);
        vk_check(vkEndCommandBuffer(frame.command_buffer));
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
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
        if (step_recorded) {
            simulation::commit_step(fields, pending_step);
            pressure_submitted[token.frame_index] = true;
        }
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
            for (auto& view : pressure_views) graphics::destroy_volume_view(view, device);
            simulation::destroy_projection(projection, device);
            simulation::destroy_pressure(pressure, device);
            for (auto& view : density_views) graphics::destroy_volume_view(view, device);
            simulation::destroy_divergence(divergence, device);
            graphics::destroy_volume_pipeline(volume_pipeline, device);
            simulation::destroy_smoke(smoke, device);
            simulation::destroy_advection(simulation, device);
            simulation::destroy_fields(fields, device);
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

graphics::VulkanBackend::VulkanBackend(void* native_window, const simulation::InitialConditions& initial,
    const simulation::SmokeParameters& smoke_parameters, std::span<const std::array<float, 3>> sources) : impl_(std::make_unique<Impl>()) {
    impl_->window = static_cast<SDL_Window*>(native_window);
    try {
        impl_->vk.create(impl_->window);
        auto& vk = impl_->vk;
        simulation::create_fields(vk.fields, vk.physical_device, vk.device, vk.queue, vk.queue_family, initial);
        const std::array fields{simulation::density_view(vk.fields, 0, 0), simulation::density_view(vk.fields, 1, 0)};
        simulation::create_advection(vk.simulation, vk.physical_device, vk.device, fields, {simulation::velocity_view(vk.fields, 0, 0), simulation::velocity_view(vk.fields, 1, 0)},
            {simulation::temperature_view(vk.fields, 0, 0), simulation::temperature_view(vk.fields, 1, 0)});
        simulation::create_smoke(vk.smoke, vk.physical_device, vk.device, vk.fields);
        vk.smoke_parameters = smoke_parameters;
        vk.sources.assign(sources.begin(), sources.end());
        create_volume_pipeline(vk.volume_pipeline, vk.physical_device, vk.device);
        const auto divergence = simulation::divergence_view(vk.fields, 0);
        simulation::create_divergence(vk.divergence, vk.physical_device, vk.device,
            {simulation::velocity_view(vk.fields, 0, 0), simulation::velocity_view(vk.fields, 1, 0)}, divergence);
        const std::array pressure_fields{simulation::pressure_view(vk.fields, 0, 0), simulation::pressure_view(vk.fields, 1, 0)};
        simulation::create_pressure(vk.pressure, vk.physical_device, vk.device, divergence, pressure_fields, vk.frames.size());
        simulation::create_projection(vk.projection, vk.physical_device, vk.device,
            {simulation::velocity_view(vk.fields, 0, 0), simulation::velocity_view(vk.fields, 1, 0)}, pressure_fields);
        for (size_t i = 0; i < pressure_fields.size(); ++i)
            create_volume_view(vk.pressure_views[i], vk.device, vk.volume_pipeline, pressure_fields[i], true, 0.25f);
        for (size_t i = 0; i < fields.size(); ++i)
            create_volume_view(vk.density_views[i], vk.device, vk.volume_pipeline, fields[i]);
    } catch (...) {
        impl_->vk.destroy();
        throw;
    }
}

graphics::VulkanBackend::~VulkanBackend() { impl_->vk.destroy(); }
graphics::Frame graphics::VulkanBackend::begin_frame() {
    return impl_->vk.begin_frame(impl_->window);
}
void graphics::VulkanBackend::end_frame(Frame frame) { impl_->vk.end_frame(frame); }
void graphics::VulkanBackend::wait_idle() { vkDeviceWaitIdle(impl_->vk.device); }

graphics::SimulationOutput graphics::VulkanBackend::step_simulation(Frame token) {
    auto& vk = impl_->vk;
    if (vk.step_recorded) throw std::logic_error("Simulation already recorded for this frame");
    vk.pending_step = simulation::prepare_advection(vk.fields);
    simulation::record_advection(vk.simulation, vk.frames[token.frame_index].command_buffer, vk.pending_step);
    simulation::record_smoke(vk.smoke, vk.frames[token.frame_index].command_buffer, vk.pending_step, vk.smoke_parameters, vk.sources);
    simulation::record_divergence(vk.divergence, vk.frames[token.frame_index].command_buffer, vk.pending_step.velocity_output);
    const auto pressure = simulation::record_pressure(vk.pressure, vk.frames[token.frame_index].command_buffer,
        vk.pending_step.dt, vk.pending_step.output.version, token.frame_index);
    simulation::record_projection(vk.projection, vk.frames[token.frame_index].command_buffer,
        vk.pending_step.velocity_output, pressure, vk.pending_step.dt);
    // Preserve divergence as a view of the final velocity, not the pre-projection RHS.
    simulation::record_divergence(vk.divergence, vk.frames[token.frame_index].command_buffer, vk.pending_step.velocity_output);
    vk.step_recorded = true;
    return {vk.pending_step.output, pressure};
}

void graphics::VulkanBackend::render_volume(Frame token, const FieldView& field, const OrbitCamera& camera, float domain_box_opacity) {
    auto& vk = impl_->vk;
    auto& frame = vk.frames[token.frame_index];
    if (frame.target.extent.width != vk.extent.width || frame.target.extent.height != vk.extent.height) {
        destroy_render_target(frame.target, vk.device);
        create_render_target(frame.target,
            {vk.volume_pipeline.render_pass, vk.volume_pipeline.color_format, vk.volume_pipeline.depth_format},
            vk.physical_device, vk.device, vk.extent);
    }
    for (const auto& view : vk.pressure_views) {
        if (same_field_binding(view.field, field)) {
            record_volume_view(view, frame.target, frame.command_buffer, camera, domain_box_opacity);
            return;
        }
    }
    for (const auto& view : vk.density_views) {
        if (same_field_binding(view.field, field)) {
            record_volume_view(view, frame.target, frame.command_buffer, camera, domain_box_opacity);
            return;
        }
    }
    throw std::invalid_argument("No volume view configured for the selected field");
}

std::string_view graphics::VulkanBackend::simulation_status() const {
    return impl_->vk.pressure_status;
}
