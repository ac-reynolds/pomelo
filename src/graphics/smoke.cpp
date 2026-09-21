#include "graphics/smoke.hpp"
#include "graphics/shaders.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
namespace {
void check(VkResult r) { if (r != VK_SUCCESS) throw std::runtime_error("Smoke Vulkan error: " + std::to_string(r)); }
struct Parameters {
    std::array<uint32_t, 3> dimensions; float dt;
    std::array<float, 3> origin; float spacing;
    std::array<float, 3> center; float radius;
    float density_rate, heat_rate, cooling, dissipation, buoyancy;
};
static_assert(sizeof(Parameters) == 68 && offsetof(Parameters, center) == 32 &&
    offsetof(Parameters, density_rate) == 48 && offsetof(Parameters, buoyancy) == 64);
void validate(const graphics::FieldDomain& domain, const simulation::SmokeParameters& p) {
    if (!std::isfinite(domain.spacing) || domain.spacing <= 0 ||
        !std::isfinite(p.radius) || p.radius < 2 * domain.spacing)
        throw std::invalid_argument("Smoke emitter must have a finite radius of at least two cells");
    for (size_t axis = 0; axis < 3; ++axis) {
        const double extent = double(domain.dimensions[axis]) * domain.spacing;
        if (!domain.dimensions[axis] || !std::isfinite(domain.origin[axis]) ||
            !std::isfinite(p.center[axis]) || p.center[axis] - double(p.radius) < domain.origin[axis] ||
            p.center[axis] + double(p.radius) > domain.origin[axis] + extent)
            throw std::invalid_argument("Smoke emitter must fit within the domain");
    }
    for (float rate : {p.density_rate, p.heat_rate, p.cooling, p.dissipation, p.buoyancy})
        if (!std::isfinite(rate) || rate < 0) throw std::invalid_argument("Invalid smoke rate");
}
}

simulation::SmokeParameters simulation::smoke_parameters(const graphics::FieldDomain& domain) {
    SmokeParameters p;
    p.radius = std::max(2 * domain.spacing,
        0.06f * std::min(domain.dimensions[0], domain.dimensions[2]) * domain.spacing);
    p.center = {domain.origin[0] + 0.5f * domain.dimensions[0] * domain.spacing,
        domain.origin[1] + p.radius + domain.spacing,
        domain.origin[2] + 0.5f * domain.dimensions[2] * domain.spacing};
    validate(domain, p);
    return p;
}

void simulation::create_smoke(Smoke& sim, VkPhysicalDevice physical, VkDevice device,
    const SimulationFields& fields) {
    const auto& domain = fields.domain;
    smoke_parameters(domain); // Reject unresolved domains before allocating resources.
    const std::array density{density_view(fields, 0, 0), density_view(fields, 1, 0)};
    const std::array velocity{velocity_view(fields, 0, 0), velocity_view(fields, 1, 0)};
    const std::array temperature{temperature_view(fields, 0, 0), temperature_view(fields, 1, 0)};
    const std::array images{density[0].image, density[1].image, velocity[0].image,
        velocity[1].image, temperature[0].image, temperature[1].image};
    for (size_t i = 0; i < images.size(); ++i) {
        if (!images[i].image || !images[i].view) throw std::invalid_argument("Missing smoke field");
        for (size_t j = 0; j < i; ++j)
            if (images[i].image == images[j].image) throw std::invalid_argument("Smoke fields must not alias");
    }
    VkShaderModule shader{};
    try {
        sim.density = density;
        sim.temperature = temperature;
        sim.velocity = velocity;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        if (properties.limits.maxComputeWorkGroupInvocations < 64)
            throw std::runtime_error("Smoke requires 64 compute invocations per workgroup");
        for (size_t axis = 0; axis < 3; ++axis)
            if (!domain.dimensions[axis] || properties.limits.maxComputeWorkGroupSize[axis] < 4 ||
                (uint64_t(domain.dimensions[axis]) + 3) / 4 > properties.limits.maxComputeWorkGroupCount[axis])
                throw std::invalid_argument("Smoke dispatch exceeds device limits");
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor_layout.bindingCount = 3;
        descriptor_layout.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &descriptor_layout, nullptr, &sim.descriptor_layout));
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 2;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &sim.pool));
        for (uint32_t i = 0; i < 2; ++i) {
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = sim.pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &sim.descriptor_layout;
            check(vkAllocateDescriptorSets(device, &allocate, &sim.sets[i]));
            const VkDescriptorImageInfo images[] = {
                {VK_NULL_HANDLE, density[i].image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, velocity[i].image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, temperature[i].image.view, VK_IMAGE_LAYOUT_GENERAL}};
            for (uint32_t binding = 0; binding < 3; ++binding) {
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = sim.sets[i];
                write.dstBinding = binding;
                write.descriptorCount = 1;
                write.descriptorType = bindings[binding].descriptorType;
                write.pImageInfo = &images[binding];
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
        }
        VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &sim.descriptor_layout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &constants;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &sim.layout));
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = smoke_comp_spirv_size;
        module.pCode = smoke_comp_spirv;
        check(vkCreateShaderModule(device, &module, nullptr, &shader));
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline.stage.module = shader;
        pipeline.stage.pName = "main";
        pipeline.layout = sim.layout;
        check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &sim.pipeline));
    } catch (...) {
        if (shader) vkDestroyShaderModule(device, shader, nullptr);
        destroy_smoke(sim, device);
        throw;
    }
    vkDestroyShaderModule(device, shader, nullptr);
}

void simulation::destroy_smoke(Smoke& sim, VkDevice device) {
    if (sim.pipeline) vkDestroyPipeline(device, sim.pipeline, nullptr);
    if (sim.layout) vkDestroyPipelineLayout(device, sim.layout, nullptr);
    if (sim.pool) vkDestroyDescriptorPool(device, sim.pool, nullptr);
    if (sim.descriptor_layout) vkDestroyDescriptorSetLayout(device, sim.descriptor_layout, nullptr);
    sim = {};
}

void simulation::record_smoke(const Smoke& sim, VkCommandBuffer command,
    const AdvectionStep& step, const SmokeParameters& p) {
    const uint32_t destination = graphics::same_field_binding(step.output, sim.density[0]) ? 0 : 1;
    if (!graphics::same_field_binding(step.output, sim.density[destination]) ||
        !graphics::same_field_binding(step.velocity_output, sim.velocity[destination]) ||
        !graphics::same_field_binding(step.temperature_output, sim.temperature[destination]) ||
        step.output.version != step.velocity_output.version || step.output.version != step.temperature_output.version)
        throw std::invalid_argument("Smoke requires matching destination bindings and versions");
    validate(step.output.domain, p);
    if (!std::isfinite(step.dt) || step.dt < 0) throw std::invalid_argument("Invalid smoke timestep");
    if (step.dt == 0) return;
    const std::array images{step.output.image.image, step.velocity_output.image.image, step.temperature_output.image.image};
    std::array<VkImageMemoryBarrier, 3> barriers{};
    for (size_t i = 0; i < barriers.size(); ++i) {
        auto& barrier = barriers[i];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = images[i];
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    // Includes the advection writes and earlier frame readers of these images.
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 3, barriers.data());
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.layout, 0, 1, &sim.sets[destination], 0, nullptr);
    const auto& domain = step.output.domain;
    const Parameters parameters{domain.dimensions, step.dt, domain.origin, domain.spacing, p.center, p.radius,
        p.enabled ? p.density_rate : 0, p.enabled ? p.heat_rate : 0, p.cooling, p.dissipation, p.buoyancy};
    vkCmdPushConstants(command, sim.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
    const auto n = domain.dimensions;
    vkCmdDispatch(command, (n[0]+3)/4, (n[1]+3)/4, (n[2]+3)/4);
    for (auto& barrier : barriers) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 3, barriers.data());
}
