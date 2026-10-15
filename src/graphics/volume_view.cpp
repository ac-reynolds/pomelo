#include "graphics/volume_view.hpp"
#include "graphics/shaders.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

void check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("Volume view Vulkan call failed: " + std::to_string(result));
}

VkFormat supported_format(VkPhysicalDevice physical, std::initializer_list<VkFormat> formats,
                          VkFormatFeatureFlags features) {
    for (VkFormat format : formats) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
        if ((properties.optimalTilingFeatures & features) == features) return format;
    }
    throw std::runtime_error("No supported offscreen attachment format");
}

// Matches the five float4 push-constant fields in the fragment shader.
struct Transform {
    std::array<float, 4> eye;
    std::array<float, 4> right;
    std::array<float, 4> up;
    std::array<float, 4> forward;
    std::array<float, 4> origin_spacing;
};
static_assert(offsetof(Transform, origin_spacing) == 64 && sizeof(Transform) == 80);

} // namespace

void graphics::create_volume_pipeline(VolumePipeline& view, VkPhysicalDevice physical, VkDevice device) {
    VkShaderModule vertex_shader{}, fragment_shader{};
    try {
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo descriptor_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor_layout.bindingCount = 1;
        descriptor_layout.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device, &descriptor_layout, nullptr, &view.field_layout));
        view.color_format = supported_format(physical, {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT);
        view.depth_format = supported_format(physical, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM},
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = view.color_format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = view.depth_format;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        subpass.pDepthStencilAttachment = &depth;
        std::array<VkSubpassDependency, 2> dependencies{};
        // Cover previous sampling, presentation transfers, and attachment writes.
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = static_cast<uint32_t>(attachments.size());
        pass.pAttachments = attachments.data();
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = static_cast<uint32_t>(dependencies.size());
        pass.pDependencies = dependencies.data();
        check(vkCreateRenderPass(device, &pass, nullptr, &view.render_pass));

        VkPushConstantRange constants{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Transform)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &view.field_layout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &constants;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &view.layout));
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = cube_vert_spirv_size;
        module.pCode = cube_vert_spirv;
        check(vkCreateShaderModule(device, &module, nullptr, &vertex_shader));
        module.codeSize = cube_frag_spirv_size;
        module.pCode = cube_frag_spirv;
        check(vkCreateShaderModule(device, &module, nullptr, &fragment_shader));
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex_shader;
        stages[0].pName = stages[1].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment_shader;
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth_state{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_state.depthTestEnable = depth_state.depthWriteEnable = VK_FALSE;
        depth_state.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;
        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = static_cast<uint32_t>(stages.size());
        pipeline.pStages = stages.data();
        pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth_state;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = view.layout;
        pipeline.renderPass = view.render_pass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &view.pipeline));
    } catch (...) {
        if (fragment_shader) vkDestroyShaderModule(device, fragment_shader, nullptr);
        if (vertex_shader) vkDestroyShaderModule(device, vertex_shader, nullptr);
        destroy_volume_pipeline(view, device);
        throw;
    }
    vkDestroyShaderModule(device, fragment_shader, nullptr);
    vkDestroyShaderModule(device, vertex_shader, nullptr);
}

void graphics::destroy_volume_pipeline(VolumePipeline& view, VkDevice device) {
    if (view.pipeline) vkDestroyPipeline(device, view.pipeline, nullptr);
    if (view.layout) vkDestroyPipelineLayout(device, view.layout, nullptr);
    if (view.render_pass) vkDestroyRenderPass(device, view.render_pass, nullptr);
    if (view.field_layout) vkDestroyDescriptorSetLayout(device, view.field_layout, nullptr);
    view = {};
}

void graphics::create_volume_view(VolumeView& view, VkDevice device, const VolumePipeline& pipeline,
                                  const FieldView& field, bool signed_values, float signed_scale) {
    if (!std::isfinite(signed_scale) || signed_scale <= 0 || !std::isfinite(1 / signed_scale))
        throw std::invalid_argument("Invalid signed volume scale");
    if (field.format != VK_FORMAT_R32_SFLOAT || !field.image.view ||
        (field.layout != VK_IMAGE_LAYOUT_GENERAL && field.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
        throw std::invalid_argument("Volume view requires a sampled scalar R32 field");
    try {
        view.pipeline = &pipeline;
        view.field = field;
        view.signed_values = signed_values;
        view.signed_scale = signed_scale;
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &view.pool));
        VkDescriptorSetAllocateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        descriptor.descriptorPool = view.pool;
        descriptor.descriptorSetCount = 1;
        descriptor.pSetLayouts = &pipeline.field_layout;
        check(vkAllocateDescriptorSets(device, &descriptor, &view.set));
        VkDescriptorImageInfo image{VK_NULL_HANDLE, field.image.view, field.layout};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = view.set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    } catch (...) { destroy_volume_view(view, device); throw; }
}

void graphics::destroy_volume_view(VolumeView& view, VkDevice device) {
    if (view.pool) vkDestroyDescriptorPool(device, view.pool, nullptr);
    view = {};
}

void graphics::record_volume_view(const VolumeView& view, const RenderTarget& target,
                                 VkCommandBuffer command_buffer, const OrbitCamera& camera, float domain_box_opacity) {
    if (!std::isfinite(domain_box_opacity) || domain_box_opacity < 0 || domain_box_opacity > 1)
        throw std::invalid_argument("Domain box opacity must be between zero and one");
    if (!std::isfinite(camera.zoom) || camera.zoom < 0.5f || camera.zoom > 3.0f)
        throw std::invalid_argument("Camera zoom must be between 0.5 and 3");
    const auto& pipeline = *view.pipeline;
    const auto& grid = view.field.domain;
    std::array<float, 4> origin_spacing{};
    std::array<float, 3> center{};
    float radius{};
    double radius_squared = 0;
    for (size_t axis = 0; axis < 3; ++axis) {
        origin_spacing[axis] = grid.origin[axis];
        const double half = 0.5 * grid.spacing * grid.dimensions[axis];
        center[axis] = static_cast<float>(grid.origin[axis] + half);
        radius_squared += half * half;
    }
    origin_spacing[3] = grid.spacing;
    radius = static_cast<float>(std::sqrt(radius_squared));
    if (!std::isfinite(radius) || radius <= 0)
        throw std::invalid_argument("Grid bounds cannot be represented for rendering");

    const float angle = camera.yaw;
    const float c = std::cos(angle), s = std::sin(angle);
    const float tilt = camera.pitch, ct = std::cos(tilt), st = std::sin(tilt);
    const float aspect = static_cast<float>(target.extent.width) / target.extent.height;
    const float tangent = std::tan(0.785398163f * 0.5f);
    const float half_fov = std::atan(tangent * std::fmin(aspect, 1.0f));
    const float distance = 1.1f * radius / (std::sin(half_fov) * camera.zoom);
    // Orbit basis, with rays expressed in grid space.
    const std::array<float, 3> right{c, 0, s};
    const std::array<float, 3> up{s * st, ct, -c * st};
    const std::array<float, 3> back{-s * ct, st, c * ct};
    const Transform transform{
        {center[0] + distance * back[0], center[1] + distance * back[1],
         center[2] + distance * back[2], view.signed_values ? 1.0f / view.signed_scale : 0.0f},
        {right[0] * tangent * aspect, right[1] * tangent * aspect, right[2] * tangent * aspect, 0},
        {up[0] * tangent, up[1] * tangent, up[2] * tangent, 0},
        {-back[0], -back[1], -back[2], domain_box_opacity},
        {origin_spacing[0], origin_spacing[1], origin_spacing[2], origin_spacing[3]}};
    std::array<VkClearValue, 2> clear{};
    clear[0].color = {{0.055f, 0.065f, 0.085f, 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = pipeline.render_pass;
    pass.framebuffer = target.framebuffer;
    pass.renderArea.extent = target.extent;
    pass.clearValueCount = static_cast<uint32_t>(clear.size());
    pass.pClearValues = clear.data();
    vkCmdBeginRenderPass(command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    VkViewport viewport{0, 0, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, target.extent};
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
                            0, 1, &view.set, 0, nullptr);
    vkCmdPushConstants(command_buffer, pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(transform), &transform);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
}
