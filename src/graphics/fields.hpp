#pragma once
#include "graphics/field_view.hpp"
#include "graphics/initial_conditions.hpp"

namespace simulation {
struct SimulationFields {
    std::array<gpu::Image, 2> density;
    std::array<gpu::Image, 2> temperature;
    // Collocated xyz components. Projection uses a forward pressure gradient
    // paired with backward divergence; advection still samples at cell centers.
    std::array<gpu::Image, 2> velocity;
    gpu::Image divergence; // Pressure RHS during the solve, corrected velocity divergence at step end.
    std::array<gpu::Image, 2> pressure;
    graphics::FieldDomain domain;
    uint64_t submitted_steps{};
};
void create_fields(SimulationFields&, VkPhysicalDevice, VkDevice, VkQueue, uint32_t, const InitialConditions&);
void destroy_fields(SimulationFields&, VkDevice);
graphics::FieldView density_view(const SimulationFields&, uint32_t index, uint64_t version);
graphics::FieldView current_density(const SimulationFields&);
graphics::FieldView temperature_view(const SimulationFields&, uint32_t index, uint64_t version);
graphics::FieldView current_temperature(const SimulationFields&);
graphics::FieldView velocity_view(const SimulationFields&, uint32_t index, uint64_t version);
graphics::FieldView current_velocity(const SimulationFields&);
graphics::FieldView pressure_view(const SimulationFields&, uint32_t index, uint64_t version);
graphics::FieldView divergence_view(const SimulationFields&, uint64_t version);
}
