#include "graphics/scene_renderer.hpp"
#include "graphics/shaders.hpp"
#include "geometry/uniform_grid.hpp"

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

// Matches the five float4 push-constant fields in the fragment shader.
struct Transform {
    std::array<float, 4> eye;
    std::array<float, 4> right;
    std::array<float, 4> up;
    std::array<float, 4> forward;
    std::array<float, 4> origin_spacing;
};
static_assert(offsetof(Transform, origin_spacing) == 64 && sizeof(Transform) == 80);

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

void upload_grid(graphics::SceneResources& scene, VkPhysicalDevice physical, VkDevice device,
                 VkQueue queue, uint32_t queue_family, const geometry::UniformGrid& grid) {
    geometry::validate_grid(grid);
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageFormatProperties supported{};
    check(vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TYPE_3D,
                                                   VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported));
    const VkExtent3D extent{grid.dimensions[0], grid.dimensions[1], grid.dimensions[2]};
    if (extent.width > supported.maxExtent.width || extent.height > supported.maxExtent.height ||
        extent.depth > supported.maxExtent.depth)
        throw std::invalid_argument("Grid exceeds Vulkan 3D image limits");
    double radius_squared = 0;
    for (size_t axis = 0; axis < 3; ++axis) {
        scene.origin_spacing[axis] = grid.origin[axis];
        const double half = 0.5 * grid.spacing * grid.dimensions[axis];
        scene.center[axis] = static_cast<float>(grid.origin[axis] + half);
        radius_squared += half * half;
    }
    scene.origin_spacing[3] = grid.spacing;
    scene.radius = static_cast<float>(std::sqrt(radius_squared));
    if (!std::isfinite(scene.radius) || scene.radius <= 0)
        throw std::invalid_argument("Grid bounds cannot be represented for rendering");

    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_3D;
    image.format = VK_FORMAT_R32_SFLOAT;
    image.extent = extent;
    image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = usage;
    check(vkCreateImage(device, &image, nullptr, &scene.grid_image.image));
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, scene.grid_image.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &allocation, nullptr, &scene.grid_image.memory));
    check(vkBindImageMemory(device, scene.grid_image.image, scene.grid_image.memory, 0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = scene.grid_image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view.format = VK_FORMAT_R32_SFLOAT;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(device, &view, nullptr, &scene.grid_image.view));

    VkBuffer staging{};
    VkDeviceMemory staging_memory{};
    VkCommandPool pool{};
    VkFence fence{};
    bool submitted = false;
    auto cleanup = [&] {
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (staging) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory) vkFreeMemory(device, staging_memory, nullptr);
    };
    try {
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = grid.values.size() * sizeof(float);
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        check(vkCreateBuffer(device, &buffer, nullptr, &staging));
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &staging_memory));
        check(vkBindBufferMemory(device, staging, staging_memory, 0));
        void* mapped{};
        check(vkMapMemory(device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        std::memcpy(mapped, grid.values.data(), static_cast<size_t>(buffer.size));
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = staging_memory;
        range.size = VK_WHOLE_SIZE;
        const VkResult flushed = vkFlushMappedMemoryRanges(device, 1, &range);
        vkUnmapMemory(device, staging_memory);
        check(flushed);
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = queue_family;
        check(vkCreateCommandPool(device, &pool_info, nullptr, &pool));
        VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commands.commandPool = pool;
        commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commands.commandBufferCount = 1;
        VkCommandBuffer command{};
        check(vkAllocateCommandBuffers(device, &commands, &command));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin));
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = scene.grid_image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = extent;
        vkCmdCopyBufferToImage(command, staging, scene.grid_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        check(vkEndCommandBuffer(command));
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device, &fence_info, nullptr, &fence));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, fence));
        submitted = true;
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    } catch (...) {
        if (submitted) vkDeviceWaitIdle(device);
        cleanup();
        throw;
    }
    cleanup();

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &scene.grid_layout));
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1};
    VkDescriptorPoolCreateInfo descriptor_pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptor_pool.maxSets = descriptor_pool.poolSizeCount = 1;
    descriptor_pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device, &descriptor_pool, nullptr, &scene.grid_pool));
    VkDescriptorSetAllocateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descriptor.descriptorPool = scene.grid_pool;
    descriptor.descriptorSetCount = 1;
    descriptor.pSetLayouts = &scene.grid_layout;
    check(vkAllocateDescriptorSets(device, &descriptor, &scene.grid_set));
    VkDescriptorImageInfo image_binding{VK_NULL_HANDLE, scene.grid_image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = scene.grid_set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image_binding;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

} // namespace

void graphics::create_scene(SceneResources& scene, VkPhysicalDevice physical, VkDevice device,
                            VkQueue queue, uint32_t queue_family, const geometry::UniformGrid& grid) {
    VkShaderModule vertex_shader{}, fragment_shader{};
    try {
        upload_grid(scene, physical, device, queue, queue_family, grid);
        scene.color_format = supported_format(physical, {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        scene.depth_format = supported_format(physical, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM},
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

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

        VkPushConstantRange constants{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Transform)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &scene.grid_layout;
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
    if (scene.grid_pool) vkDestroyDescriptorPool(device, scene.grid_pool, nullptr);
    if (scene.grid_layout) vkDestroyDescriptorSetLayout(device, scene.grid_layout, nullptr);
    destroy_image(scene.grid_image, device);
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
    const float aspect = static_cast<float>(target.extent.width) / target.extent.height;
    const float tangent = std::tan(0.785398163f * 0.5f);
    const float half_fov = std::atan(tangent * std::fmin(aspect, 1.0f));
    const float distance = 1.1f * scene.radius / std::sin(half_fov);
    // Inverse of the previous view rotation, with rays expressed in grid space.
    const std::array<float, 3> right{c, 0, s};
    const std::array<float, 3> up{s * st, ct, -c * st};
    const std::array<float, 3> back{-s * ct, st, c * ct};
    const Transform transform{
        {scene.center[0] + distance * back[0], scene.center[1] + distance * back[1],
         scene.center[2] + distance * back[2], 0},
        {right[0] * tangent * aspect, right[1] * tangent * aspect, right[2] * tangent * aspect, 0},
        {up[0] * tangent, up[1] * tangent, up[2] * tangent, 0},
        {-back[0], -back[1], -back[2], 0},
        {scene.origin_spacing[0], scene.origin_spacing[1], scene.origin_spacing[2], scene.origin_spacing[3]}};
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
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene.layout,
                            0, 1, &scene.grid_set, 0, nullptr);
    vkCmdPushConstants(command_buffer, scene.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(transform), &transform);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
}
