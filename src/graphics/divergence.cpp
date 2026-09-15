#include "graphics/divergence.hpp"
#include "graphics/shaders.hpp"
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
namespace {
void check(VkResult r) { if (r != VK_SUCCESS) throw std::runtime_error("Divergence Vulkan error: " + std::to_string(r)); }
struct Parameters { std::array<uint32_t, 3> dimensions; float inverse_spacing; };
static_assert(sizeof(Parameters) == 16 && offsetof(Parameters, inverse_spacing) == 12);
}
void simulation::create_divergence(Divergence& sim, VkPhysicalDevice physical, VkDevice device,
    const std::array<graphics::FieldView, 2>& velocity, const graphics::FieldView& output) {
    const auto& domain = output.domain;
    if (!output.image.view || output.format != VK_FORMAT_R32_SFLOAT || output.layout != VK_IMAGE_LAYOUT_GENERAL ||
        !std::isfinite(1.0f / domain.spacing) || domain.spacing <= 0)
        throw std::invalid_argument("Invalid divergence output field");
    for (const auto& field : velocity)
        if (!field.image.view || field.format != VK_FORMAT_R32G32B32A32_SFLOAT || field.layout != VK_IMAGE_LAYOUT_GENERAL ||
            field.domain.dimensions != domain.dimensions || field.domain.spacing != domain.spacing ||
            field.domain.origin != domain.origin)
            throw std::invalid_argument("Incompatible divergence velocity field");
    VkShaderModule shader{};
    try {
        sim.output = output;
        sim.velocity = velocity;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        if (properties.limits.maxComputeWorkGroupInvocations < 64)
            throw std::runtime_error("Divergence requires 64 compute invocations per workgroup");
        for (size_t axis = 0; axis < 3; ++axis)
            if (!domain.dimensions[axis] || properties.limits.maxComputeWorkGroupSize[axis] < 4 ||
                (uint64_t(domain.dimensions[axis]) + 3) / 4 > properties.limits.maxComputeWorkGroupCount[axis])
                throw std::invalid_argument("Divergence dispatch exceeds device limits");
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor_layout.bindingCount = 2;
        descriptor_layout.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &descriptor_layout, nullptr, &sim.descriptor_layout));
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
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
                {VK_NULL_HANDLE, velocity[i].image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, output.image.view, VK_IMAGE_LAYOUT_GENERAL}};
            for (uint32_t binding = 0; binding < 2; ++binding) {
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
        module.codeSize = divergence_comp_spirv_size;
        module.pCode = divergence_comp_spirv;
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
        destroy_divergence(sim, device);
        throw;
    }
    vkDestroyShaderModule(device, shader, nullptr);
}

void simulation::destroy_divergence(Divergence& sim, VkDevice device) {
    if (sim.pipeline) vkDestroyPipeline(device, sim.pipeline, nullptr);
    if (sim.layout) vkDestroyPipelineLayout(device, sim.layout, nullptr);
    if (sim.pool) vkDestroyDescriptorPool(device, sim.pool, nullptr);
    if (sim.descriptor_layout) vkDestroyDescriptorSetLayout(device, sim.descriptor_layout, nullptr);
    sim = {};
}

void simulation::record_divergence(const Divergence& sim, VkCommandBuffer command, const graphics::FieldView& velocity) {
    const uint32_t source = graphics::same_field_binding(velocity, sim.velocity[0]) ? 0 : 1;
    if (!graphics::same_field_binding(velocity, sim.velocity[source]))
        throw std::invalid_argument("Unknown divergence velocity binding");
    std::array<VkImageMemoryBarrier, 2> barriers{};
    for (auto& barrier : barriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    barriers[0].image = velocity.image.image;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].image = sim.output.image.image;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    // The single output is shared across frames. Finish prior fragment reads
    // before overwriting it, even when those reads belong to an earlier submission.
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers.data());
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.layout, 0, 1, &sim.sets[source], 0, nullptr);
    const auto n = velocity.domain.dimensions;
    const Parameters parameters{n, 1.0f / velocity.domain.spacing};
    vkCmdPushConstants(command, sim.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
    vkCmdDispatch(command, (n[0]+3)/4, (n[1]+3)/4, (n[2]+3)/4);
    auto& ready = barriers[1];
    ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &ready);
}
