#include "simulation/advection.hpp"
#include "simulation/shaders.hpp"
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace {
void check(VkResult r) {
    if (r != VK_SUCCESS) throw std::runtime_error("Advection Vulkan call failed: " + std::to_string(r));
}
struct Step { std::array<uint32_t, 3> dimensions; float dt_over_spacing; };
static_assert(sizeof(Step) == 16 && offsetof(Step, dt_over_spacing) == 12);
}

void simulation::create_advection(Advection& sim, VkPhysicalDevice physical, VkDevice device,
    VkQueue queue, uint32_t queue_family, const InitialConditions& initial) {
    validate_initial_conditions(initial);
    VkShaderModule shader{};
    try {
        sim.dimensions = initial.density.dimensions;
        sim.spacing = initial.density.spacing;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        if (properties.limits.maxComputeWorkGroupInvocations < 64)
            throw std::runtime_error("Advection requires 64 compute invocations per workgroup");
        for (size_t axis = 0; axis < 3; ++axis)
            if (properties.limits.maxComputeWorkGroupSize[axis] < 4 ||
                (uint64_t(sim.dimensions[axis]) + 3) / 4 > properties.limits.maxComputeWorkGroupCount[axis])
                throw std::invalid_argument("Advection dispatch exceeds device limits");
        const VkExtent3D extent{sim.dimensions[0], sim.dimensions[1], sim.dimensions[2]};
        for (auto& density : sim.density) {
            gpu::create_image(density, physical, device, extent, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            gpu::upload_image(density, physical, device, queue, queue_family, extent,
                initial.density.values.data(), initial.density.values.size() * sizeof(float), VK_IMAGE_LAYOUT_GENERAL);
        }
        gpu::create_image(sim.velocity, physical, device, extent, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        gpu::upload_image(sim.velocity, physical, device, queue, queue_family, extent,
            initial.velocity.data(), initial.velocity.size() * sizeof(initial.velocity[0]),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor_layout.bindingCount = 3;
        descriptor_layout.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &descriptor_layout, nullptr, &sim.descriptor_layout));
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 2;
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &sim.pool));
        for (uint32_t i = 0; i < 2; ++i) {
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = sim.pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &sim.descriptor_layout;
            check(vkAllocateDescriptorSets(device, &allocate, &sim.sets[i]));
            const VkDescriptorImageInfo images[] = {
                {VK_NULL_HANDLE, sim.density[i].view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, sim.velocity.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {VK_NULL_HANDLE, sim.density[1-i].view, VK_IMAGE_LAYOUT_GENERAL}};
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
        VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Step)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &sim.descriptor_layout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &constants;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &sim.layout));
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = advect_comp_spirv_size;
        module.pCode = advect_comp_spirv;
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
        destroy_advection(sim, device);
        throw;
    }
    vkDestroyShaderModule(device, shader, nullptr);
}

void simulation::destroy_advection(Advection& sim, VkDevice device) {
    if (sim.pipeline) vkDestroyPipeline(device, sim.pipeline, nullptr);
    if (sim.layout) vkDestroyPipelineLayout(device, sim.layout, nullptr);
    if (sim.pool) vkDestroyDescriptorPool(device, sim.pool, nullptr);
    if (sim.descriptor_layout) vkDestroyDescriptorSetLayout(device, sim.descriptor_layout, nullptr);
    for (auto& density : sim.density) gpu::destroy_image(density, device);
    gpu::destroy_image(sim.velocity, device);
    sim = {};
}

uint32_t simulation::record_advection(const Advection& sim, VkCommandBuffer command, float dt) {
    const float ratio = dt / sim.spacing;
    if (!std::isfinite(ratio) || dt < 0) throw std::invalid_argument("Invalid advection timestep");
    const uint32_t source = sim.submitted_steps % 2, destination = 1 - source;
    std::array<VkImageMemoryBarrier, 2> barriers{};
    for (uint32_t i = 0; i < 2; ++i) {
        auto& barrier = barriers[i];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = sim.density[i].image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = i == source ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_SHADER_WRITE_BIT;
    }
    // Includes earlier submissions on this queue, particularly fragment reads of
    // the image that is about to become the destination again.
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers.data());
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.layout, 0, 1, &sim.sets[source], 0, nullptr);
    const Step step{sim.dimensions, ratio};
    vkCmdPushConstants(command, sim.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(step), &step);
    vkCmdDispatch(command, (sim.dimensions[0] + 3) / 4, (sim.dimensions[1] + 3) / 4, (sim.dimensions[2] + 3) / 4);
    auto& ready = barriers[destination];
    ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &ready);
    return destination;
}
