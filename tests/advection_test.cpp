#include "graphics/advection.hpp"
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
// Independent weighted eight-corner CPU reference, in double precision.
std::vector<float> reference(const simulation::InitialConditions& initial, const std::vector<float>& old, float dt) {
    auto& grid = initial.density;
    std::vector<float> result(old.size());
    for (uint32_t z = 0; z < grid.dimensions[2]; ++z)
        for (uint32_t y = 0; y < grid.dimensions[1]; ++y)
            for (uint32_t x = 0; x < grid.dimensions[0]; ++x) {
                const size_t index = geometry::cell_index(grid, x, y, z);
                const double p[] = {x - double(dt) / grid.spacing * initial.velocity[index][0],
                    y - double(dt) / grid.spacing * initial.velocity[index][1],
                    z - double(dt) / grid.spacing * initial.velocity[index][2]};
                double value = 0;
                for (int corner = 0; corner < 8; ++corner) {
                    uint32_t node[3]; double weight = 1;
                    for (int axis = 0; axis < 3; ++axis) {
                        const int high = (corner >> axis) & 1;
                        const double f = p[axis] - std::floor(p[axis]);
                        weight *= high ? f : 1-f;
                        int n = (int(std::floor(p[axis])) + high) % int(grid.dimensions[axis]);
                        if (n < 0) n += grid.dimensions[axis];
                        node[axis] = n;
                    }
                    value += weight * old[geometry::cell_index(grid, node[0], node[1], node[2])];
                }
                result[index] = float(value);
            }
    return result;
}
void run(Context& c, const char* name, const simulation::InitialConditions& initial, unsigned steps, float dt) {
    simulation::SimulationFields fields;
    simulation::Advection sim;
    graphics::VolumePipeline pipeline;
    std::array<graphics::VolumeView, 2> volume_views;
    auto cleanup = [&] {
        vkDeviceWaitIdle(c.device);
        for (auto& slot : c.slots) graphics::destroy_render_target(slot.target, c.device);
        for (auto& view : volume_views) graphics::destroy_volume_view(view, c.device);
        graphics::destroy_volume_pipeline(pipeline, c.device);
        simulation::destroy_advection(sim, c.device);
        simulation::destroy_fields(fields, c.device);
    };
    try {
        simulation::create_fields(fields, c.physical, c.device, c.queue, c.family, initial);
        const std::array views{simulation::density_view(fields, 0, 0), simulation::density_view(fields, 1, 0)};
        simulation::create_advection(sim, c.physical, c.device, views, {simulation::velocity_view(fields, 0, 0), simulation::velocity_view(fields, 1, 0)});
        graphics::create_volume_pipeline(pipeline, c.physical, c.device);
        for (size_t i = 0; i < views.size(); ++i)
            graphics::create_volume_view(volume_views[i], c.device, pipeline, views[i]);
        for (auto& slot : c.slots) graphics::create_render_target(slot.target, {pipeline.render_pass, pipeline.color_format, pipeline.depth_format}, c.physical, c.device, {16, 16});
        auto expected = initial.density.values;
        auto reference_state = initial;
        for (unsigned i = 0; i < steps; ++i) {
            auto& slot = c.slots[i % 2]; begin(c, slot);
            // Recreate only the retired target while another frame can still be in flight.
            if (i == 3) {
                graphics::destroy_render_target(slot.target, c.device);
                graphics::create_render_target(slot.target, {pipeline.render_pass, pipeline.color_format, pipeline.depth_format}, c.physical, c.device, {20, 12});
            }
            const auto step = simulation::prepare_advection(fields, dt);
            simulation::record_advection(sim, slot.command, step);
            require(fields.submitted_steps == i, "Recording committed simulation state prematurely");
            const auto output = step.output;
            require(output.image.view == fields.density[(i + 1) % 2].view, "Wrong output parity");
            require(step.velocity_output.image.view == fields.velocity[(i + 1) % 2].view &&
                    step.velocity_output.version == step.output.version, "Velocity parity/version mismatch");
            graphics::record_volume_view(volume_views[(i + 1) % 2], slot.target, slot.command, {});
            submit(c, slot); simulation::commit_step(fields, step);
            if (i == 0) {
                bool rejected = false;
                try { simulation::commit_step(fields, step); }
                catch (const std::logic_error&) { rejected = true; }
                require(rejected, "Committed the same field version twice");
            }
            expected = reference(reference_state, expected, dt);
            auto next_velocity = reference_state.velocity;
            for (size_t component = 0; component < 3; ++component) {
                std::vector<float> values(expected.size());
                for (size_t cell = 0; cell < values.size(); ++cell)
                    values[cell] = reference_state.velocity[cell][component];
                const auto advected = reference(reference_state, values, dt);
                for (size_t cell = 0; cell < values.size(); ++cell)
                    next_velocity[cell][component] = advected[cell];
            }
            reference_state.velocity = std::move(next_velocity);
        }
        const VkExtent3D extent{fields.domain.dimensions[0], fields.domain.dimensions[1], fields.domain.dimensions[2]};
        auto actual = read_image(c, fields.density[fields.submitted_steps % 2], extent, 1, VK_IMAGE_LAYOUT_GENERAL);
        float error = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            require(std::isfinite(actual[i]) && actual[i] >= -1e-6f && actual[i] <= 1.000001f, "Density out of range");
            error = std::max(error, std::abs(actual[i] - expected[i]));
        }
        // Repeated backtracing of the discontinuous periodic ramp accumulates
        // float-coordinate rounding relative to the double-precision reference.
        const float tolerance = std::strcmp(name, "linear shear") == 0 ? 1e-4f : 1e-5f;
        require(error < tolerance, "GPU differs from reference");
        if (std::strcmp(name, "linear shear") == 0) {
            float change = 0;
            for (size_t i = 0; i < actual.size(); ++i)
                change = std::max(change, std::abs(actual[i] - initial.density.values[i]));
            require(change > 0.1f, "Shear did not evolve density");
            // The bottom row has zero velocity and must remain stationary.
            for (uint32_t x = 0; x < fields.domain.dimensions[0]; ++x)
                require(std::abs(actual[x] - initial.density.values[x]) < 1e-6f,
                        "Zero-velocity row moved");
        }
        auto velocity = read_image(c, fields.velocity[fields.submitted_steps % 2], extent, 4, VK_IMAGE_LAYOUT_GENERAL);
        float velocity_error = 0, velocity_change = 0;
        for (size_t cell = 0; cell < expected.size(); ++cell) {
            for (size_t component = 0; component < 4; ++component) {
                const float value = velocity[4 * cell + component];
                require(std::isfinite(value), "Nonfinite velocity");
                velocity_error = std::max(velocity_error, std::abs(value - reference_state.velocity[cell][component]));
                velocity_change = std::max(velocity_change, std::abs(value - initial.velocity[cell][component]));
            }
        }
        require(velocity_error < tolerance, "Advected velocity differs from CPU reference");
        if (std::strcmp(name, "velocity self-advection") == 0)
            require(velocity_change > 0.01f, "Velocity self-advection did not evolve the field");
        if (std::strcmp(name, "integer periodic shift") == 0) {
            // Both renderer descriptors must actually select their respective images.
            // A stale or hard-coded descriptor would produce identical pixels here.
            std::array<std::vector<float>, 2> pixels;
            for (uint32_t image = 0; image < 2; ++image) {
                auto& slot = c.slots[0]; begin(c, slot);
                graphics::record_volume_view(volume_views[image], slot.target, slot.command, {});
                submit(c, slot);
                pixels[image] = read_image(c, slot.target.color,
                    {slot.target.extent.width, slot.target.extent.height, 1}, 1,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            // RGBA8 bytes occupy the readback vector's storage; compare bit patterns.
            require(std::memcmp(pixels[0].data(), pixels[1].data(), pixels[0].size()*sizeof(float)) != 0,
                    "Renderer did not distinguish old and new density images");
        }
        const double mass = std::accumulate(actual.begin(), actual.end(), 0.0);
        const double initial_mass = std::accumulate(initial.density.values.begin(), initial.density.values.end(), 0.0);
        std::printf("%s: %u steps, max error %.3g, mass drift %.3g, velocity error %.3g\n", name, steps, error, mass-initial_mass, velocity_error);
        cleanup();
    } catch (...) { cleanup(); throw; }
}
}
int main() {
    Context c;
    try {
        if (!create(c)) { destroy(c); std::puts("No graphics/compute Vulkan device; skipped"); return 77; }
        const auto defaults = simulation::initial_conditions();
        require(defaults.density.dimensions == std::array<uint32_t, 3>{64, 64, 64} &&
                defaults.density.spacing == 2.0f / 64, "Default grid and domain");
        run(c, "default resolution", defaults, 1, simulation::timestep);
        // Keep the long numerical regression at its explicit reference resolution.
        auto shear = simulation::initial_conditions({24, 24, 24}, {-1, -1, -1}, 2.0f / 24);
        const auto& grid = shear.density;
        require(grid.values.front() == 0 && grid.values[23] == 1, "Density ramp endpoints");
        require(shear.velocity.front()[0] == 0 && shear.velocity[24*23][0] == 0.5f,
                "Velocity ramp endpoints");
        run(c, "linear shear", shear, 60, simulation::timestep);
        std::fill(shear.density.values.begin(), shear.density.values.end(), 0.5f);
        run(c, "uniform shear", shear, 600, simulation::timestep);
        auto fixture = simulation::initial_conditions({7, 5, 3}, {-2, 1, 4}, 0.25f);
        for (size_t i = 0; i < fixture.density.values.size(); ++i)
            fixture.density.values[i] = float((i * 17) % 101) / 100;
        run(c, "zero timestep", fixture, 3, 0);
        for (auto& v : fixture.velocity) v = {};
        run(c, "zero velocity", fixture, 3, 0.25f);
        for (int axis = 0; axis < 3; ++axis) {
            for (auto& v : fixture.velocity) { v = {}; v[axis] = axis == 1 ? -1 : 1; }
            run(c, "integer periodic shift", fixture, 7, 0.25f);
        }
        for (auto& v : fixture.velocity) v = {0.3f, -0.7f, 0.2f, 0};
        run(c, "fractional transport", fixture, 11, 0.25f);
        auto smooth = simulation::initial_conditions({9, 7, 5}, {}, 0.25f);
        for (uint32_t z = 0; z < 5; ++z)
            for (uint32_t y = 0; y < 7; ++y)
                for (uint32_t x = 0; x < 9; ++x) {
                    const double r2 = std::pow((double(x)-4)/2, 2) + std::pow((double(y)-3)/2, 2) + std::pow(double(z)-2, 2);
                    smooth.density.values[geometry::cell_index(smooth.density, x, y, z)] = float(std::exp(-r2));
                }
        for (auto& v : smooth.velocity) v = {0.3f, -0.7f, 0.2f, 0};
        run(c, "smooth localized transport", smooth, 13, 0.25f);
        auto evolving = simulation::initial_conditions({9, 7, 5}, {}, 0.25f);
        for (uint32_t z = 0; z < 5; ++z)
            for (uint32_t y = 0; y < 7; ++y)
                for (uint32_t x = 0; x < 9; ++x) {
                    // Initially divergence-free periodic flow with nonzero self-advection.
                    const auto cell = geometry::cell_index(evolving.density, x, y, z);
                    evolving.velocity[cell] = {float(0.3 * std::sin(6.283185307 * y / 7)),
                        float(0.2 * std::cos(6.283185307 * z / 5)),
                        float(-0.4 * std::sin(6.283185307 * x / 9)), 0};
                }
        run(c, "velocity self-advection", evolving, 17, 0.1f);
        auto thin = simulation::initial_conditions({1, 5, 7}, {}, 0.25f);
        thin.density.values[2] = 1;
        run(c, "singleton and variable velocity", thin, 9, 0.1f);
        destroy(c);
        require(c.errors == 0, "Vulkan validation errors");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); destroy(c); return 1; }
}
