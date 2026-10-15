// Deterministic, surface-free capture of the application's Vulkan simulation and
// volume renderer. No SDL window, display server, alternate solver, or CPU image
// reconstruction is involved. The only readback is the final rendered image.
#include "application/input_deck.hpp"
#include "graphics/advection.hpp"
#include "graphics/divergence.hpp"
#include "graphics/pressure.hpp"
#include "graphics/projection.hpp"
#include "graphics/smoke.hpp"
#include "graphics/volume_view.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("Capture Vulkan error: " + std::to_string(result));
}
uint32_t integer(std::string_view text, const char* label, uint32_t minimum, uint32_t maximum) {
    uint32_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < minimum || value > maximum)
        throw std::invalid_argument(std::string(label) + ": expected integer in [" +
            std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
    return value;
}
struct Context {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkFence fence{};
    VkDebugUtilsMessengerEXT messenger{};
    unsigned validation_errors{};

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            ++static_cast<Context*>(user)->validation_errors;
            std::fprintf(stderr, "%s\n", data->pMessage);
        }
        return VK_FALSE;
    }
    void create() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Pomelo capture";
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        uint32_t count{};
        check(vkEnumerateInstanceLayerProperties(&count, nullptr));
        std::vector<VkLayerProperties> layers(count);
        check(vkEnumerateInstanceLayerProperties(&count, layers.data()));
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const bool validation = std::any_of(layers.begin(), layers.end(), [&](const auto& p) {
            return std::strcmp(p.layerName, layer) == 0;
        });
        const VkValidationFeatureEnableEXT feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = &feature;
        const char* extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        if (validation) {
            info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer;
            info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = extensions;
            info.pNext = &features;
        }
        check(vkCreateInstance(&info, nullptr, &instance));
        if (validation) {
            VkDebugUtilsMessengerCreateInfoEXT debug_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            debug_info.pfnUserCallback = debug;
            debug_info.pUserData = this;
            check(reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,
                "vkCreateDebugUtilsMessengerEXT"))(instance, &debug_info, nullptr, &messenger));
        }
        std::printf("Synchronization validation: %s\n", validation ? "enabled" : "unavailable");
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        for (const auto candidate : devices) {
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, queues.data());
            for (uint32_t i = 0; i < count; ++i) {
                constexpr auto flags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
                if ((queues[i].queueFlags & flags) == flags) { physical = candidate; family = i; break; }
            }
            if (physical) break;
        }
        if (!physical) throw std::runtime_error("No graphics/compute Vulkan device was found");
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::printf("GPU: %s\n", properties.deviceName);
        float priority = 1;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
        check(vkCreateDevice(physical, &device_info, nullptr, &device));
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pool_info, nullptr, &pool));
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &allocate, &command));
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fence_info, nullptr, &fence));
    }
    void begin() {
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
        check(vkResetCommandPool(device, pool, 0));
        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin_info));
    }
    void submit() {
        check(vkEndCommandBuffer(command));
        check(vkResetFences(device, 1, &fence));
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1; info.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &info, fence));
    }
    void wait() { check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX)); }
    void destroy() {
        if (device) {
            vkDeviceWaitIdle(device);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (messenger) reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,
            "vkDestroyDebugUtilsMessengerEXT"))(instance, messenger, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        instance = {}; device = {}; messenger = {}; fence = {}; pool = {};
    }
    ~Context() { destroy(); }
};

struct Scene {
    Context& c;
    simulation::SimulationFields fields;
    simulation::Advection advection;
    simulation::Smoke smoke;
    simulation::Divergence divergence;
    simulation::PressureSolve pressure;
    simulation::Projection projection;
    graphics::VolumePipeline pipeline;
    std::array<graphics::VolumeView, 2> views;
    graphics::RenderTarget target;
    explicit Scene(Context& context) : c(context) {}
    ~Scene() {
        if (!c.device) return;
        vkDeviceWaitIdle(c.device);
        graphics::destroy_render_target(target, c.device);
        for (auto& view : views) graphics::destroy_volume_view(view, c.device);
        graphics::destroy_volume_pipeline(pipeline, c.device);
        simulation::destroy_projection(projection, c.device);
        simulation::destroy_pressure(pressure, c.device);
        simulation::destroy_divergence(divergence, c.device);
        simulation::destroy_smoke(smoke, c.device);
        simulation::destroy_advection(advection, c.device);
        simulation::destroy_fields(fields, c.device);
    }
    void create(const simulation::InitialConditions& initial, VkExtent2D extent) {
        simulation::create_fields(fields, c.physical, c.device, c.queue, c.family, initial);
        const std::array density{simulation::density_view(fields, 0, 0), simulation::density_view(fields, 1, 0)};
        const std::array velocity{simulation::velocity_view(fields, 0, 0), simulation::velocity_view(fields, 1, 0)};
        const std::array temperature{simulation::temperature_view(fields, 0, 0), simulation::temperature_view(fields, 1, 0)};
        const std::array pressures{simulation::pressure_view(fields, 0, 0), simulation::pressure_view(fields, 1, 0)};
        simulation::create_advection(advection, c.physical, c.device, density, velocity, temperature);
        simulation::create_smoke(smoke, c.physical, c.device, fields);
        simulation::create_divergence(divergence, c.physical, c.device, velocity, simulation::divergence_view(fields, 0));
        simulation::create_pressure(pressure, c.physical, c.device, simulation::divergence_view(fields, 0), pressures, 1);
        simulation::create_projection(projection, c.physical, c.device, velocity, pressures);
        graphics::create_volume_pipeline(pipeline, c.physical, c.device);
        for (size_t i = 0; i < views.size(); ++i)
            graphics::create_volume_view(views[i], c.device, pipeline, density[i]);
        graphics::create_render_target(target, {pipeline.render_pass, pipeline.color_format, pipeline.depth_format},
            c.physical, c.device, extent);
    }
};

std::vector<unsigned char> pixels(Context& c, const graphics::RenderTarget& target, VkFormat format) {
    const size_t bytes = size_t(target.extent.width) * target.extent.height * 4;
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    const auto cleanup = [&] {
        if (buffer) vkDestroyBuffer(c.device, buffer, nullptr);
        if (memory) vkFreeMemory(c.device, memory, nullptr);
    };
    try {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bytes; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(c.device, &info, nullptr, &buffer));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(c.device, buffer, &requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(c.physical, &properties);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { type = i; break; }
        if (type == UINT32_MAX) throw std::runtime_error("No host-visible image readback memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
        check(vkAllocateMemory(c.device, &allocation, nullptr, &memory));
        check(vkBindBufferMemory(c.device, buffer, memory, 0));
        c.begin();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = target.color.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(c.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {target.extent.width, target.extent.height, 1};
        vkCmdCopyImageToBuffer(c.command, target.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(c.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host, 0, nullptr, 0, nullptr);
        c.submit(); c.wait();
        void* mapped{};
        check(vkMapMemory(c.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory; range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(c.device, 1, &range));
        std::vector<unsigned char> result(bytes);
        std::memcpy(result.data(), mapped, bytes);
        vkUnmapMemory(c.device, memory);
        if (format == VK_FORMAT_B8G8R8A8_UNORM)
            for (size_t i = 0; i < bytes; i += 4) std::swap(result[i], result[i + 2]);
        else if (format != VK_FORMAT_R8G8B8A8_UNORM)
            throw std::runtime_error("Capture expects an RGBA8 or BGRA8 render target");
        cleanup();
        return result;
    } catch (...) { vkDeviceWaitIdle(c.device); cleanup(); throw; }
}

void write_ppm(const char* filename, VkExtent2D extent, const std::vector<unsigned char>& image) {
    std::ofstream output(filename, std::ios::binary);
    if (!output) throw std::runtime_error(std::string("Cannot write capture: ") + filename);
    output << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
    for (size_t i = 0; i < image.size(); i += 4)
        output.write(reinterpret_cast<const char*>(image.data() + i), 3);
    output.close();
    if (!output) throw std::runtime_error(std::string("Capture write failed: ") + filename);
}

void capture(const application::Settings& deck, uint32_t frames, const char* filename, VkExtent2D extent, uint32_t checkpoint_interval) {
    const auto domain = simulation::smoke_domain();
    application::validate_settings(deck, domain);
    const auto initial = application::make_initial_conditions(deck, domain);
    const auto smoke_parameters = application::make_smoke_parameters(deck, domain);
    const graphics::OrbitCamera camera{deck.camera_yaw, deck.camera_pitch, deck.camera_zoom};
    Context c;
    c.create();
    const simulation::PressureSettings pressure_settings{.iterations = deck.pressure_iterations};
    std::printf("Pressure iterations per step: %u\n", pressure_settings.iterations);
    std::vector<unsigned char> image;
    {
        Scene scene(c);
        scene.create(initial, extent);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            c.begin();
            const auto step = simulation::prepare_advection(scene.fields);
            // Deliberately the same ordering and deck pressure settings as
            // VulkanBackend::step_simulation in the interactive executable.
            simulation::record_advection(scene.advection, c.command, step);
            simulation::record_smoke(scene.smoke, c.command, step, smoke_parameters, deck.sources);
            simulation::record_divergence(scene.divergence, c.command, step.velocity_output);
            const auto pressure = simulation::record_pressure(scene.pressure, c.command, step.dt, step.output.version, 0, pressure_settings);
            simulation::record_projection(scene.projection, c.command, step.velocity_output, pressure, step.dt);
            simulation::record_divergence(scene.divergence, c.command, step.velocity_output);
            const bool checkpoint = checkpoint_interval && (frame + 1) % checkpoint_interval == 0;
            if (frame + 1 == frames || checkpoint)
                graphics::record_volume_view(scene.views[(frame + 1) % 2], scene.target, c.command,
                    camera, deck.domain_box_opacity);
            c.submit();
            simulation::commit_step(scene.fields, step);
            c.wait();
            const auto diagnostics = simulation::read_pressure_diagnostics(scene.pressure, c.device, 0);
            if (!diagnostics.finite) throw std::runtime_error("Simulation produced nonfinite pressure diagnostics");
            if (checkpoint && frame + 1 != frames) {
                const auto snapshot = pixels(c, scene.target, scene.pipeline.color_format);
                if (c.validation_errors) throw std::runtime_error("Vulkan validation rejected checkpoint");
                const std::filesystem::path output_path(filename);
                char suffix[32];
                std::snprintf(suffix, sizeof(suffix), "-frame%06u", frame + 1);
                const auto checkpoint_path = output_path.parent_path() /
                    (output_path.stem().string() + suffix + output_path.extension().string());
                write_ppm(checkpoint_path.string().c_str(), extent, snapshot);
                std::printf("Saved checkpoint %s\n", checkpoint_path.string().c_str());
            }
            if ((frame + 1) % 30 == 0 || frame + 1 == frames) {
                std::printf("Frame %u/%u, t=%.6f s, pressure residual=%.7g, RHS RMS=%.7g\n",
                    frame + 1, frames, (frame + 1) * double(simulation::timestep),
                    diagnostics.residual_rms, diagnostics.rhs_rms);
                std::fflush(stdout);
            }
        }
        if (frames == 0) {
            c.begin();
            graphics::record_volume_view(scene.views[0], scene.target, c.command, camera, deck.domain_box_opacity);
            c.submit(); c.wait();
            std::puts("Initial state, t=0 s");
        }
        image = pixels(c, scene.target, scene.pipeline.color_format);
    }
    c.destroy();
    if (c.validation_errors) throw std::runtime_error("Vulkan validation reported " +
        std::to_string(c.validation_errors) + " errors; capture rejected");
    write_ppm(filename, extent, image);
    std::printf("Saved %s (%u x %u), validation errors: %u\n", filename, extent.width, extent.height, c.validation_errors);
}
}

int main(int argc, char** argv) {
    constexpr const char* usage = "Usage: pomelo_capture <input.json> <frames> <output.ppm> [width height [checkpoint_interval]]";
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        std::puts(usage);
        return 0;
    }
    try {
        if (argc != 4 && argc != 6 && argc != 7) throw std::invalid_argument(usage);
        const auto frames = integer(argv[2], "frames", 0, 1000000);
        const VkExtent2D extent{argc >= 6 ? integer(argv[4], "width", 1, 8192) : 1280,
                                argc >= 6 ? integer(argv[5], "height", 1, 8192) : 960};
        const auto checkpoint_interval = argc == 7 ? integer(argv[6], "checkpoint_interval", 1, 1000000) : 0;
        capture(application::load_input_deck(argv[1]), frames, argv[3], extent, checkpoint_interval);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pomelo_capture: %s\n", error.what());
        return 1;
    }
}
