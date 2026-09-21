#include "graphics/fields.hpp"
#include <stdexcept>

void simulation::create_fields(SimulationFields& fields, VkPhysicalDevice physical, VkDevice device,
    VkQueue queue, uint32_t queue_family, const InitialConditions& initial) {
    validate_initial_conditions(initial);
    fields.domain = {initial.density.dimensions, initial.density.origin, initial.density.spacing};
    try {
        const VkExtent3D extent{fields.domain.dimensions[0], fields.domain.dimensions[1], fields.domain.dimensions[2]};
        for (auto& density : fields.density) {
            gpu::create_image(density, physical, device, extent, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            gpu::upload_image(density, physical, device, queue, queue_family, extent,
                initial.density.values.data(), initial.density.values.size() * sizeof(float), VK_IMAGE_LAYOUT_GENERAL);
        }
        for (auto& temperature : fields.temperature) {
            gpu::create_image(temperature, physical, device, extent, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            gpu::upload_image(temperature, physical, device, queue, queue_family, extent,
                initial.temperature.data(), initial.temperature.size() * sizeof(float), VK_IMAGE_LAYOUT_GENERAL);
        }
        for (auto& velocity : fields.velocity) {
            gpu::create_image(velocity, physical, device, extent, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            gpu::upload_image(velocity, physical, device, queue, queue_family, extent,
                initial.velocity.data(), initial.velocity.size() * sizeof(initial.velocity[0]),
                VK_IMAGE_LAYOUT_GENERAL);
        }
        gpu::create_image(fields.divergence, physical, device, extent, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        const std::vector<float> zero(initial.density.values.size(), 0.0f);
        gpu::upload_image(fields.divergence, physical, device, queue, queue_family, extent,
            zero.data(), zero.size() * sizeof(float), VK_IMAGE_LAYOUT_GENERAL);
        for (auto& pressure : fields.pressure) {
            gpu::create_image(pressure, physical, device, extent, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            gpu::upload_image(pressure, physical, device, queue, queue_family, extent,
                zero.data(), zero.size() * sizeof(float), VK_IMAGE_LAYOUT_GENERAL);
        }
    } catch (...) { destroy_fields(fields, device); throw; }
}
void simulation::destroy_fields(SimulationFields& fields, VkDevice device) {
    for (auto& image : fields.density) gpu::destroy_image(image, device);
    for (auto& image : fields.temperature) gpu::destroy_image(image, device);
    for (auto& image : fields.velocity) gpu::destroy_image(image, device);
    gpu::destroy_image(fields.divergence, device);
    for (auto& image : fields.pressure) gpu::destroy_image(image, device);
    fields = {};
}
graphics::FieldView simulation::density_view(const SimulationFields& fields, uint32_t index, uint64_t version) {
    return {fields.density.at(index), VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, fields.domain, version};
}
graphics::FieldView simulation::current_density(const SimulationFields& fields) {
    return density_view(fields, fields.submitted_steps % 2, fields.submitted_steps);
}
graphics::FieldView simulation::velocity_view(const SimulationFields& fields, uint32_t index, uint64_t version) {
    return {fields.velocity.at(index), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, fields.domain, version};
}
graphics::FieldView simulation::current_velocity(const SimulationFields& fields) {
    return velocity_view(fields, fields.submitted_steps % 2, fields.submitted_steps);
}
graphics::FieldView simulation::divergence_view(const SimulationFields& fields, uint64_t version) {
    return {fields.divergence, VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, fields.domain, version};
}

graphics::FieldView simulation::pressure_view(const SimulationFields& fields, uint32_t index, uint64_t version) {
    return {fields.pressure.at(index), VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, fields.domain, version};
}
graphics::FieldView simulation::temperature_view(const SimulationFields& fields, uint32_t index, uint64_t version) {
    return {fields.temperature.at(index), VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, fields.domain, version};
}
graphics::FieldView simulation::current_temperature(const SimulationFields& fields) {
    return temperature_view(fields, fields.submitted_steps % 2, fields.submitted_steps);
}
