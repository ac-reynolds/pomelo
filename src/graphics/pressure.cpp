#include "graphics/pressure.hpp"
#include "graphics/shaders.hpp"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
void check(VkResult r) {
    if (r != VK_SUCCESS) throw std::runtime_error("Pressure Vulkan error: " + std::to_string(r));
}
struct Parameters {
    std::array<uint32_t, 3> dimensions;
    float inverse_dt, spacing_squared, omega;
    uint32_t groups, mode;
};
static_assert(sizeof(Parameters) == 32 && offsetof(Parameters, inverse_dt) == 12);
void barrier(VkCommandBuffer command, VkPipelineStageFlags source, VkAccessFlags reads,
             VkPipelineStageFlags destination, VkAccessFlags writes) {
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memory.srcAccessMask = reads;
    memory.dstAccessMask = writes;
    vkCmdPipelineBarrier(command, source, destination, 0, 1, &memory, 0, nullptr, 0, nullptr);
}
void compute_barrier(VkCommandBuffer command) {
    barrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}
void create_buffer(simulation::PressureBuffer& buffer, VkPhysicalDevice physical, VkDevice device,
                   VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size; info.usage = usage;
    check(vkCreateBuffer(device, &info, nullptr, &buffer.buffer));
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) {
            type = i; break;
        }
    if (type == UINT32_MAX) throw std::runtime_error("No suitable pressure buffer memory");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
    check(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory));
    check(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0));
}
void destroy_buffer(simulation::PressureBuffer& buffer, VkDevice device) {
    if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}
void create_pipeline(VkDevice device, VkPipelineLayout layout, const uint32_t* code, size_t bytes, VkPipeline& pipeline) {
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = bytes; module.pCode = code;
    VkShaderModule shader{};
    check(vkCreateShaderModule(device, &module, nullptr, &shader));
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = shader; info.stage.pName = "main"; info.layout = layout;
    auto result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, shader, nullptr);
    check(result);
}
}

bool simulation::pressure_converged(const PressureDiagnostics& d, const PressureSettings& settings) {
    return d.finite && d.residual_rms <= settings.absolute_tolerance + settings.relative_tolerance * d.rhs_rms;
}

void simulation::create_pressure(PressureSolve& sim, VkPhysicalDevice physical, VkDevice device,
    const graphics::FieldView& divergence, const std::array<graphics::FieldView, 2>& pressure, uint32_t frame_slots) {
    const auto& domain = divergence.domain;
    const float h2 = domain.spacing * domain.spacing;
    if (!frame_slots || domain.spacing <= 0 || !std::isfinite(h2) || h2 <= 0 || !std::isfinite(1 / h2))
        throw std::invalid_argument("Invalid pressure domain or frame slots");
    for (const auto& field : {divergence, pressure[0], pressure[1]})
        if (!field.image.image || !field.image.view || field.format != VK_FORMAT_R32_SFLOAT ||
            field.layout != VK_IMAGE_LAYOUT_GENERAL || field.domain.dimensions != domain.dimensions ||
            field.domain.spacing != domain.spacing || field.domain.origin != domain.origin)
            throw std::invalid_argument("Incompatible pressure field");
    if (pressure[0].image.image == pressure[1].image.image || pressure[0].image.image == divergence.image.image ||
        pressure[1].image.image == divergence.image.image)
        throw std::invalid_argument("Pressure requires distinct input and output images");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    const auto& limits = properties.limits;
    uint64_t cells = 1;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!domain.dimensions[axis] || limits.maxComputeWorkGroupSize[axis] < 4 ||
            (uint64_t(domain.dimensions[axis]) + 3) / 4 > limits.maxComputeWorkGroupCount[axis])
            throw std::invalid_argument("Pressure dispatch exceeds device limits");
        cells *= domain.dimensions[axis];
        if (cells > UINT32_MAX - 1023u) throw std::invalid_argument("Pressure volume too large");
    }
    if (limits.maxComputeWorkGroupInvocations < 64 || limits.maxComputeWorkGroupSize[0] < 64 ||
        (cells + 1023) / 1024 > limits.maxComputeWorkGroupCount[0] ||
        ((cells + 1023) / 1024) * 16 > limits.maxStorageBufferRange)
        throw std::invalid_argument("Pressure reduction exceeds device limits");
    try {
        sim.divergence = divergence; sim.pressure = pressure;
        sim.groups = uint32_t((cells + 1023) / 1024);
        create_buffer(sim.partials, physical, device, VkDeviceSize(sim.groups) * 16,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        create_buffer(sim.statistics, physical, device, 32,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        sim.readbacks.resize(frame_slots);
        for (auto& readback : sim.readbacks)
            create_buffer(readback, physical, device, 32, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor.bindingCount = 5; descriptor.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &descriptor, nullptr, &sim.descriptor_layout));
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 2; pool.poolSizeCount = 3; pool.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &sim.pool));
        for (uint32_t i = 0; i < 2; ++i) {
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = sim.pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &sim.descriptor_layout;
            check(vkAllocateDescriptorSets(device, &allocate, &sim.sets[i]));
            const VkDescriptorImageInfo images[] = {{VK_NULL_HANDLE, divergence.image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, pressure[i].image.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, pressure[1-i].image.view, VK_IMAGE_LAYOUT_GENERAL}};
            const VkDescriptorBufferInfo buffers[] = {{sim.partials.buffer, 0, VkDeviceSize(sim.groups) * 16},
                {sim.statistics.buffer, 0, 32}};
            for (uint32_t binding = 0; binding < 5; ++binding) {
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = sim.sets[i]; write.dstBinding = binding; write.descriptorCount = 1;
                write.descriptorType = bindings[binding].descriptorType;
                if (binding < 3) write.pImageInfo = &images[binding];
                else write.pBufferInfo = &buffers[binding-3];
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
        }
        VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1; layout.pSetLayouts = &sim.descriptor_layout;
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &constants;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &sim.layout));
        create_pipeline(device, sim.layout, pressure_comp_spirv, pressure_comp_spirv_size, sim.jacobi);
        create_pipeline(device, sim.layout, pressure_reduce_comp_spirv, pressure_reduce_comp_spirv_size, sim.reduce);
        create_pipeline(device, sim.layout, pressure_statistics_comp_spirv, pressure_statistics_comp_spirv_size, sim.statistics_pipeline);
        create_pipeline(device, sim.layout, pressure_normalize_comp_spirv, pressure_normalize_comp_spirv_size, sim.normalize);
    } catch (...) { destroy_pressure(sim, device); throw; }
}
void simulation::destroy_pressure(PressureSolve& sim, VkDevice device) {
    for (auto pipeline : {sim.jacobi, sim.reduce, sim.statistics_pipeline, sim.normalize})
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    if (sim.layout) vkDestroyPipelineLayout(device, sim.layout, nullptr);
    if (sim.pool) vkDestroyDescriptorPool(device, sim.pool, nullptr);
    if (sim.descriptor_layout) vkDestroyDescriptorSetLayout(device, sim.descriptor_layout, nullptr);
    destroy_buffer(sim.partials, device); destroy_buffer(sim.statistics, device);
    for (auto& readback : sim.readbacks) destroy_buffer(readback, device);
    sim = {};
}

graphics::FieldView simulation::record_pressure(const PressureSolve& sim, VkCommandBuffer command,
    float dt, uint64_t version, uint32_t frame_slot, const PressureSettings& settings) {
    if (dt < 0 || !std::isfinite(dt) || (dt > 0 && !std::isfinite(1 / dt)) ||
        !settings.iterations || !std::isfinite(settings.omega) || settings.omega <= 0 || settings.omega >= 1 ||
        !std::isfinite(settings.absolute_tolerance) || settings.absolute_tolerance < 0 ||
        !std::isfinite(settings.relative_tolerance) || settings.relative_tolerance < 0)
        throw std::invalid_argument("Invalid pressure timestep or settings");
    const auto& readback = sim.readbacks.at(frame_slot);
    const auto n = sim.divergence.domain.dimensions;
    Parameters parameters{n, dt > 0 ? 1 / dt : 0, sim.divergence.domain.spacing * sim.divergence.domain.spacing,
        settings.omega, sim.groups, 0};
    // Order shared resources across submissions, including prior fragment sampling
    // and diagnostics copies, before clearing or overwriting them.
    barrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (const auto& field : sim.pressure)
        vkCmdClearColorImage(command, field.image.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    vkCmdFillBuffer(command, sim.statistics.buffer, 0, 32, 0);
    barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    auto bind = [&](uint32_t index) {
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.layout, 0, 1, &sim.sets[index], 0, nullptr);
    };
    auto push = [&] {
        vkCmdPushConstants(command, sim.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
    };
    auto reduction = [&](uint32_t mode, uint32_t index) {
        parameters.mode = mode; bind(index); push();
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.reduce);
        vkCmdDispatch(command, sim.groups, 1, 1);
        compute_barrier(command);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.statistics_pipeline);
        vkCmdDispatch(command, 1, 1, 1);
        compute_barrier(command);
    };
    uint32_t index = 0;
    if (dt > 0) {
        reduction(0, 0);
        // Measure the corrected RHS directly; E[d²]-E[d]² loses precision for
        // nearly constant fields and can incorrectly report a zero norm.
        reduction(3, 0);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.jacobi);
        for (uint32_t iteration = 0; iteration < settings.iterations; ++iteration) {
            bind(index);
            vkCmdDispatch(command, (n[0]+3)/4, (n[1]+3)/4, (n[2]+3)/4);
            compute_barrier(command);
            index = 1-index;
        }
        reduction(1, index);
        bind(1-index); // Normalize the final image through the writable binding.
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sim.normalize);
        vkCmdDispatch(command, (n[0]+3)/4, (n[1]+3)/4, (n[2]+3)/4);
        compute_barrier(command);
        reduction(2, index);
    }
    barrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
    VkBufferCopy copy{0, 0, 32};
    vkCmdCopyBuffer(command, sim.statistics.buffer, readback.buffer, 1, &copy);
    barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    auto output = sim.pressure[index]; output.version = version;
    return output;
}

simulation::PressureDiagnostics simulation::read_pressure_diagnostics(const PressureSolve& sim,
    VkDevice device, uint32_t frame_slot) {
    const auto& buffer = sim.readbacks.at(frame_slot);
    void* mapped{};
    check(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = buffer.memory; range.size = VK_WHOLE_SIZE;
    const auto result = vkInvalidateMappedMemoryRanges(device, 1, &range);
    float values[8]{};
    if (result == VK_SUCCESS) std::memcpy(values, mapped, sizeof(values));
    vkUnmapMemory(device, buffer.memory);
    check(result);
    bool finite = values[4] == 0;
    for (float value : values) finite = finite && std::isfinite(value);
    return {values[0], values[1], values[2], values[3], finite};
}
