#include "graphics/scene_renderer.hpp"
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
        throw std::runtime_error("Scene Vulkan call failed: " + std::to_string(result));
}

uint32_t memory_type(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No suitable Vulkan memory type for scene resources");
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

struct Vertex { float position[3]; float color[3]; };
constexpr std::array<Vertex, 24> vertices{{
    {{-1,-1, 1}, {0.95f,0.32f,0.20f}}, {{ 1,-1, 1}, {0.95f,0.32f,0.20f}},
    {{ 1, 1, 1}, {0.95f,0.32f,0.20f}}, {{-1, 1, 1}, {0.95f,0.32f,0.20f}},
    {{ 1,-1,-1}, {0.25f,0.65f,0.95f}}, {{-1,-1,-1}, {0.25f,0.65f,0.95f}},
    {{-1, 1,-1}, {0.25f,0.65f,0.95f}}, {{ 1, 1,-1}, {0.25f,0.65f,0.95f}},
    {{ 1,-1, 1}, {0.95f,0.70f,0.18f}}, {{ 1,-1,-1}, {0.95f,0.70f,0.18f}},
    {{ 1, 1,-1}, {0.95f,0.70f,0.18f}}, {{ 1, 1, 1}, {0.95f,0.70f,0.18f}},
    {{-1,-1,-1}, {0.55f,0.35f,0.90f}}, {{-1,-1, 1}, {0.55f,0.35f,0.90f}},
    {{-1, 1, 1}, {0.55f,0.35f,0.90f}}, {{-1, 1,-1}, {0.55f,0.35f,0.90f}},
    {{-1, 1, 1}, {0.30f,0.85f,0.50f}}, {{ 1, 1, 1}, {0.30f,0.85f,0.50f}},
    {{ 1, 1,-1}, {0.30f,0.85f,0.50f}}, {{-1, 1,-1}, {0.30f,0.85f,0.50f}},
    {{-1,-1,-1}, {0.20f,0.75f,0.80f}}, {{ 1,-1,-1}, {0.20f,0.75f,0.80f}},
    {{ 1,-1, 1}, {0.20f,0.75f,0.80f}}, {{-1,-1, 1}, {0.20f,0.75f,0.80f}},
}};
constexpr auto indices = [] {
    std::array<uint16_t, 36> result{};
    for (uint16_t face = 0; face < 6; ++face) {
        constexpr uint16_t triangle[] = {0, 1, 2, 2, 3, 0};
        for (size_t i = 0; i < 6; ++i) result[face * 6 + i] = face * 4 + triangle[i];
    }
    return result;
}();

// Column-major matrices, multiplied by column vectors in the vertex shader.
using Matrix = std::array<float, 16>;
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result{};
    for (size_t col = 0; col < 4; ++col)
        for (size_t row = 0; row < 4; ++row)
            for (size_t k = 0; k < 4; ++k)
                result[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
    return result;
}

void create_image(graphics::SceneImage& image, VkPhysicalDevice physical, VkDevice device,
                  VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    check(vkCreateImage(device, &info, nullptr, &image.image));
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &allocation, nullptr, &image.memory));
    check(vkBindImageMemory(device, image.image, image.memory, 0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    check(vkCreateImageView(device, &view, nullptr, &image.view));
}

void destroy_image(graphics::SceneImage& image, VkDevice device) {
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = {};
}

} // namespace

void graphics::create_scene(SceneResources& scene, VkPhysicalDevice physical, VkDevice device) {
    VkShaderModule vertex_shader{}, fragment_shader{};
    try {
        scene.color_format = supported_format(physical, {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        scene.depth_format = supported_format(physical, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM},
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = sizeof(vertices) + sizeof(indices);
        buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        check(vkCreateBuffer(device, &buffer, nullptr, &scene.mesh));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, scene.mesh, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &scene.mesh_memory));
        check(vkBindBufferMemory(device, scene.mesh, scene.mesh_memory, 0));
        void* mapped{};
        check(vkMapMemory(device, scene.mesh_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        std::memcpy(mapped, vertices.data(), sizeof(vertices));
        std::memcpy(static_cast<std::byte*>(mapped) + sizeof(vertices), indices.data(), sizeof(indices));
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = scene.mesh_memory;
        range.size = VK_WHOLE_SIZE;
        const VkResult flushed = vkFlushMappedMemoryRanges(device, 1, &range);
        vkUnmapMemory(device, scene.mesh_memory);
        check(flushed);

        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = scene.color_format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = scene.depth_format;
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
        // Cover previous sampling and attachment writes when reusing a target.
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
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
        check(vkCreateRenderPass(device, &pass, nullptr, &scene.render_pass));

        VkPushConstantRange constants{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Matrix)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &constants;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &scene.layout));
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
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription, 2> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
        }};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth_state{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_state.depthTestEnable = depth_state.depthWriteEnable = VK_TRUE;
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
        pipeline.layout = scene.layout;
        pipeline.renderPass = scene.render_pass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &scene.pipeline));
    } catch (...) {
        if (fragment_shader) vkDestroyShaderModule(device, fragment_shader, nullptr);
        if (vertex_shader) vkDestroyShaderModule(device, vertex_shader, nullptr);
        destroy_scene(scene, device);
        throw;
    }
    vkDestroyShaderModule(device, fragment_shader, nullptr);
    vkDestroyShaderModule(device, vertex_shader, nullptr);
}

void graphics::destroy_scene(SceneResources& scene, VkDevice device) {
    if (scene.pipeline) vkDestroyPipeline(device, scene.pipeline, nullptr);
    if (scene.layout) vkDestroyPipelineLayout(device, scene.layout, nullptr);
    if (scene.render_pass) vkDestroyRenderPass(device, scene.render_pass, nullptr);
    if (scene.mesh) vkDestroyBuffer(device, scene.mesh, nullptr);
    if (scene.mesh_memory) vkFreeMemory(device, scene.mesh_memory, nullptr);
    scene = {};
}

void graphics::create_scene_target(SceneTarget& target, const SceneResources& scene,
                                  VkPhysicalDevice physical, VkDevice device, VkExtent2D extent) {
    try {
        target.extent = extent;
        create_image(target.color, physical, device, extent, scene.color_format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        create_image(target.depth, physical, device, extent, scene.depth_format,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        const VkImageView views[] = {target.color.view, target.depth.view};
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = scene.render_pass;
        framebuffer.attachmentCount = 2;
        framebuffer.pAttachments = views;
        framebuffer.width = extent.width;
        framebuffer.height = extent.height;
        framebuffer.layers = 1;
        check(vkCreateFramebuffer(device, &framebuffer, nullptr, &target.framebuffer));
    } catch (...) {
        destroy_scene_target(target, device);
        throw;
    }
}

void graphics::destroy_scene_target(SceneTarget& target, VkDevice device) {
    if (target.framebuffer) vkDestroyFramebuffer(device, target.framebuffer, nullptr);
    destroy_image(target.depth, device);
    destroy_image(target.color, device);
    target = {};
}

void graphics::record_scene(const SceneResources& scene, const SceneTarget& target,
                           VkCommandBuffer command_buffer, double elapsed_seconds) {
    const float angle = static_cast<float>(std::fmod(elapsed_seconds * 0.7, 6.283185307179586));
    const float c = std::cos(angle), s = std::sin(angle);
    const float tilt = -0.35f, ct = std::cos(tilt), st = std::sin(tilt);
    const Matrix rotation_y{c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1};
    const Matrix rotation_x{1,0,0,0, 0,ct,st,0, 0,-st,ct,0, 0,0,0,1};
    const float aspect = static_cast<float>(target.extent.width) / target.extent.height;
    // Fit the cube even in a portrait viewport. The camera looks down -Z.
    const float distance = 5.5f / std::fmin(aspect, 1.0f);
    const Matrix view{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-distance,1};
    constexpr float near_plane = 0.1f;
    const float far_plane = distance + 10.0f;
    const float f = 1.0f / std::tan(0.785398163f * 0.5f);
    // Flip Y for Vulkan's positive-height viewport; depth maps to [0, 1].
    const Matrix projection{f/aspect,0,0,0, 0,-f,0,0,
        0,0,far_plane/(near_plane-far_plane),-1,
        0,0,near_plane*far_plane/(near_plane-far_plane),0};
    const Matrix mvp = multiply(projection, multiply(view, multiply(rotation_x, rotation_y)));
    std::array<VkClearValue, 2> clear{};
    clear[0].color = {{0.055f, 0.065f, 0.085f, 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = scene.render_pass;
    pass.framebuffer = target.framebuffer;
    pass.renderArea.extent = target.extent;
    pass.clearValueCount = static_cast<uint32_t>(clear.size());
    pass.pClearValues = clear.data();
    vkCmdBeginRenderPass(command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene.pipeline);
    VkViewport viewport{0, 0, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, target.extent};
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &scene.mesh, &offset);
    vkCmdBindIndexBuffer(command_buffer, scene.mesh, sizeof(vertices), VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(command_buffer, scene.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), mvp.data());
    vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    vkCmdEndRenderPass(command_buffer);
}
