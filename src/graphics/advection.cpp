#include "graphics/advection.hpp"
#include "graphics/shaders.hpp"
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
    const std::array<graphics::FieldView, 2>& density, const std::array<graphics::FieldView, 2>& velocity) {
    const auto& domain = density[0].domain;
    if (density[0].image.image == density[1].image.image || !std::isfinite(domain.spacing) || domain.spacing <= 0)
        throw std::invalid_argument("Advection requires distinct images and positive spacing");
    for (const auto& field : density)
        if (!field.image.image || !field.image.view || field.format != VK_FORMAT_R32_SFLOAT ||
            field.layout != VK_IMAGE_LAYOUT_GENERAL || field.domain.dimensions != domain.dimensions ||
            field.domain.origin != domain.origin || field.domain.spacing != domain.spacing)
            throw std::invalid_argument("Incompatible advection density field");
    if (velocity[0].image.image == velocity[1].image.image)
        throw std::invalid_argument("Advection requires distinct velocity images");
    for (const auto& field : velocity)
        if (!field.image.image || !field.image.view || field.format != VK_FORMAT_R32G32B32A32_SFLOAT ||
            field.layout != VK_IMAGE_LAYOUT_GENERAL || field.domain.dimensions != domain.dimensions ||
            field.domain.origin != domain.origin || field.domain.spacing != domain.spacing)
            throw std::invalid_argument("Incompatible advection velocity field");
    VkShaderModule shader{};
    try {
        sim.inputs = density;
        sim.velocity = velocity;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        if (properties.limits.maxComputeWorkGroupInvocations < 64)
            throw std::runtime_error("Advection requires 64 compute invocations per workgroup");
        for (size_t axis = 0; axis < 3; ++axis)
            if (!domain.dimensions[axis] || properties.limits.maxComputeWorkGroupSize[axis] < 4 ||
                (uint64_t(density[0].domain.dimensions[axis]) + 3) / 4 > properties.limits.maxComputeWorkGroupCount[axis])
                throw std::invalid_argument("Advection dispatch exceeds device limits");
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor_layout.bindingCount = 4;
        descriptor_layout.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &descriptor_layout, nullptr, &sim.descriptor_layout));
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}};
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
                {VK_NULL_HANDLE, sim.inputs[i].image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, sim.velocity[i].image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, sim.inputs[1-i].image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, sim.velocity[1-i].image.view, VK_IMAGE_LAYOUT_GENERAL}};
            for (uint32_t binding = 0; binding < 4; ++binding) {
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
    sim = {};
}

void simulation::record_advection(const Advection& sim, VkCommandBuffer command, const AdvectionStep& operation) {
    const float ratio = operation.dt / operation.input.domain.spacing;
    const float dt = operation.dt;
    if (!std::isfinite(ratio) || dt < 0) throw std::invalid_argument("Invalid advection timestep");
    const uint32_t source = operation.input.image.view == sim.inputs[0].image.view ? 0 : 1;
    const uint32_t destination = 1 - source;
    if (!graphics::same_field_binding(operation.input, sim.inputs[source]) ||
        !graphics::same_field_binding(operation.output, sim.inputs[destination]) ||
        !graphics::same_field_binding(operation.velocity, sim.velocity[source]) ||
        !graphics::same_field_binding(operation.velocity_output, sim.velocity[destination]))
        throw std::invalid_argument("Advection fields do not match immutable bindings");
    const auto dimensions = operation.input.domain.dimensions;
    std::array<VkImageMemoryBarrier, 4> barriers{};
    for (uint32_t i = 0; i < 4; ++i) {
        auto& barrier = barriers[i];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = i < 2 ? sim.inputs[i].image.image : sim.velocity[i - 2].image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = i % 2 == source ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_SHADER_WRITE_BIT;
    }
    // Includes earlier submissions on this queue, particularly fragment reads of
    // the image that is about to become the destination again.
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 4, barriers.data());
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.layout, 0, 1, &sim.sets[source], 0, nullptr);
    const Step step{dimensions, ratio};
    vkCmdPushConstants(command, sim.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(step), &step);
    vkCmdDispatch(command, (dimensions[0] + 3) / 4, (dimensions[1] + 3) / 4, (dimensions[2] + 3) / 4);
    std::array<VkImageMemoryBarrier, 2> ready{barriers[destination], barriers[2 + destination]};
    for (auto& barrier : ready) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, ready.data());

}

simulation::AdvectionStep simulation::prepare_advection(const SimulationFields& fields, float dt) {
    return {current_density(fields), current_velocity(fields),
            density_view(fields, 1 - fields.submitted_steps % 2, fields.submitted_steps + 1),
            velocity_view(fields, 1 - fields.submitted_steps % 2, fields.submitted_steps + 1), dt};
}
void simulation::commit_step(SimulationFields& fields, const AdvectionStep& step) {
    if (step.input.version != fields.submitted_steps ||
        step.input.image.view != current_density(fields).image.view ||
        step.output.image.view != fields.density[1 - fields.submitted_steps % 2].view ||
        step.output.version != fields.submitted_steps + 1 ||
        step.velocity.version != fields.submitted_steps ||
        !graphics::same_field_binding(step.velocity, current_velocity(fields)) ||
        step.velocity_output.version != fields.submitted_steps + 1 ||
        !graphics::same_field_binding(step.velocity_output,
            velocity_view(fields, 1 - fields.submitted_steps % 2, fields.submitted_steps + 1)))
        throw std::logic_error("Cannot commit stale advection step");
    ++fields.submitted_steps;
}
