#include "vulkan_test_helpers.hpp"

namespace {
// Independent weighted eight-corner CPU reference, in double precision.
std::vector<float> reference(const simulation::InitialConditions& initial, const std::vector<float>& old, float dt) {
    auto& grid = initial.density;
    std::vector<float> result(old.size());
    for (uint32_t z = 0; z < grid.dimensions[2]; ++z)
        for (uint32_t y = 0; y < grid.dimensions[1]; ++y)
            for (uint32_t x = 0; x < grid.dimensions[0]; ++x) {
                const size_t index = geometry::cell_index(grid, x, y, z);
                const double p[] = {x - double(dt) / grid.spacing * initial.velocity[index][0],
                    y - double(dt) / grid.spacing * initial.velocity[index][1],
                    z - double(dt) / grid.spacing * initial.velocity[index][2]};
                double value = 0;
                for (int corner = 0; corner < 8; ++corner) {
                    uint32_t node[3]; double weight = 1;
                    for (int axis = 0; axis < 3; ++axis) {
                        const int high = (corner >> axis) & 1;
                        const double f = p[axis] - std::floor(p[axis]);
                        weight *= high ? f : 1-f;
                        int n = (int(std::floor(p[axis])) + high) % int(grid.dimensions[axis]);
                        if (n < 0) n += grid.dimensions[axis];
                        node[axis] = n;
                    }
                    value += weight * old[geometry::cell_index(grid, node[0], node[1], node[2])];
                }
                result[index] = float(value);
            }
    return result;
}
void run(Context& c, const char* name, const simulation::InitialConditions& initial, unsigned steps, float dt) {
    simulation::SimulationFields fields;
    simulation::Advection sim;
    simulation::Divergence divergence;
    graphics::VolumeView divergence_volume;
    graphics::VolumePipeline pipeline;
    std::array<graphics::VolumeView, 2> volume_views;
    auto cleanup = [&] {
        vkDeviceWaitIdle(c.device);
        for (auto& slot : c.slots) graphics::destroy_render_target(slot.target, c.device);
        for (auto& view : volume_views) graphics::destroy_volume_view(view, c.device);
        graphics::destroy_volume_view(divergence_volume, c.device);
        graphics::destroy_volume_pipeline(pipeline, c.device);
        simulation::destroy_divergence(divergence, c.device);
        simulation::destroy_advection(sim, c.device);
        simulation::destroy_fields(fields, c.device);
    };
    try {
        simulation::create_fields(fields, c.physical, c.device, c.queue, c.family, initial);
        const std::array views{simulation::density_view(fields, 0, 0), simulation::density_view(fields, 1, 0)};
        simulation::create_advection(sim, c.physical, c.device, views, {simulation::velocity_view(fields, 0, 0), simulation::velocity_view(fields, 1, 0)},
            {simulation::temperature_view(fields, 0, 0), simulation::temperature_view(fields, 1, 0)});
        simulation::create_divergence(divergence, c.physical, c.device,
            {simulation::velocity_view(fields, 0, 0), simulation::velocity_view(fields, 1, 0)},
            simulation::divergence_view(fields, 0));
        graphics::create_volume_pipeline(pipeline, c.physical, c.device);
        graphics::create_volume_view(divergence_volume, c.device, pipeline, simulation::divergence_view(fields, 0), true);
        for (size_t i = 0; i < views.size(); ++i)
            graphics::create_volume_view(volume_views[i], c.device, pipeline, views[i]);
        for (auto& slot : c.slots) graphics::create_render_target(slot.target, {pipeline.render_pass, pipeline.color_format, pipeline.depth_format}, c.physical, c.device, {16, 16});
        auto expected = initial.density.values;
        auto expected_temperature = initial.temperature;
        auto reference_state = initial;
        for (unsigned i = 0; i < steps; ++i) {
            auto& slot = c.slots[i % 2]; begin(c, slot);
            // Recreate only the retired target while another frame can still be in flight.
            if (i == 3) {
                graphics::destroy_render_target(slot.target, c.device);
                graphics::create_render_target(slot.target, {pipeline.render_pass, pipeline.color_format, pipeline.depth_format}, c.physical, c.device, {20, 12});
            }
            const auto step = simulation::prepare_advection(fields, dt);
            simulation::record_advection(sim, slot.command, step);
            simulation::record_divergence(divergence, slot.command, step.velocity_output);
            graphics::record_volume_view(divergence_volume, slot.target, slot.command, {});
            require(fields.submitted_steps == i, "Recording committed simulation state prematurely");
            const auto output = step.output;
            require(output.image.view == fields.density[(i + 1) % 2].view, "Wrong output parity");
            require(step.velocity_output.image.view == fields.velocity[(i + 1) % 2].view &&
                    step.velocity_output.version == step.output.version, "Velocity parity/version mismatch");
            graphics::record_volume_view(volume_views[(i + 1) % 2], slot.target, slot.command, {});
            auto stale = step;
            ++stale.temperature_output.version;
            bool rejected_temperature = false;
            try { simulation::commit_step(fields, stale); }
            catch (const std::logic_error&) { rejected_temperature = true; }
            require(rejected_temperature, "Committed stale temperature version");
            submit(c, slot); simulation::commit_step(fields, step);
            if (i == 0) {
                bool rejected = false;
                try { simulation::commit_step(fields, step); }
                catch (const std::logic_error&) { rejected = true; }
                require(rejected, "Committed the same field version twice");
            }
            expected = reference(reference_state, expected, dt);
            expected_temperature = reference(reference_state, expected_temperature, dt);
            auto next_velocity = reference_state.velocity;
            for (size_t component = 0; component < 3; ++component) {
                std::vector<float> values(expected.size());
                for (size_t cell = 0; cell < values.size(); ++cell)
                    values[cell] = reference_state.velocity[cell][component];
                const auto advected = reference(reference_state, values, dt);
                for (size_t cell = 0; cell < values.size(); ++cell)
                    next_velocity[cell][component] = advected[cell];
            }
            reference_state.velocity = std::move(next_velocity);
        }
        const VkExtent3D extent{fields.domain.dimensions[0], fields.domain.dimensions[1], fields.domain.dimensions[2]};
        auto actual = read_image(c, fields.density[fields.submitted_steps % 2], extent, 1, VK_IMAGE_LAYOUT_GENERAL);
        float error = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            require(std::isfinite(actual[i]) && actual[i] >= -1e-6f && actual[i] <= 1.000001f, "Density out of range");
            error = std::max(error, std::abs(actual[i] - expected[i]));
        }
        // Repeated backtracing of the discontinuous periodic ramp accumulates
        // float-coordinate rounding relative to the double-precision reference.
        const float tolerance = std::strcmp(name, "linear shear") == 0 ? 1e-4f : 1e-5f;
        require(error < tolerance, "GPU differs from reference");
        if (std::strcmp(name, "linear shear") == 0) {
            float change = 0;
            for (size_t i = 0; i < actual.size(); ++i)
                change = std::max(change, std::abs(actual[i] - initial.density.values[i]));
            require(change > 0.1f, "Shear did not evolve density");
            // The bottom row has zero velocity and must remain stationary.
            for (uint32_t x = 0; x < fields.domain.dimensions[0]; ++x)
                require(std::abs(actual[x] - initial.density.values[x]) < 1e-6f,
                        "Zero-velocity row moved");
        }
        const auto temperature = read_image(c, simulation::current_temperature(fields).image, extent, 1, VK_IMAGE_LAYOUT_GENERAL);
        for (size_t cell = 0; cell < temperature.size(); ++cell)
            require(std::isfinite(temperature[cell]) && std::abs(temperature[cell] - expected_temperature[cell]) < tolerance * 4,
                "Advected temperature differs from CPU reference");
        auto velocity = read_image(c, fields.velocity[fields.submitted_steps % 2], extent, 4, VK_IMAGE_LAYOUT_GENERAL);
        float velocity_error = 0, velocity_change = 0;
        for (size_t cell = 0; cell < expected.size(); ++cell) {
            for (size_t component = 0; component < 4; ++component) {
                const float value = velocity[4 * cell + component];
                require(std::isfinite(value), "Nonfinite velocity");
                velocity_error = std::max(velocity_error, std::abs(value - reference_state.velocity[cell][component]));
                velocity_change = std::max(velocity_change, std::abs(value - initial.velocity[cell][component]));
            }
        }
        require(velocity_error < tolerance, "Advected velocity differs from CPU reference");
        const auto divergence_values = read_image(c, fields.divergence, extent, 1, VK_IMAGE_LAYOUT_GENERAL);
        float divergence_error = 0, magnitude = 0;
        const auto n = fields.domain.dimensions;
        for (uint32_t z = 0; z < n[2]; ++z)
            for (uint32_t y = 0; y < n[1]; ++y)
                for (uint32_t x = 0; x < n[0]; ++x) {
                    const std::array<uint32_t, 3> cell{x, y, z};
                    double expected_divergence = 0;
                    for (size_t axis = 0; axis < 3; ++axis) {
                        auto lo = cell, hi = cell;
                        lo[axis] = (cell[axis] + n[axis] - 1) % n[axis];
                        const auto l = geometry::cell_index(initial.density, lo[0], lo[1], lo[2]);
                        const auto h = geometry::cell_index(initial.density, hi[0], hi[1], hi[2]);
                        expected_divergence += (double(velocity[4*h+axis]) - velocity[4*l+axis]) / fields.domain.spacing;
                    }
                    const auto index = geometry::cell_index(initial.density, x, y, z);
                    require(std::isfinite(divergence_values[index]), "Nonfinite divergence");
                    divergence_error = std::max(divergence_error, float(std::abs(divergence_values[index] - expected_divergence)));
                    magnitude = std::max(magnitude, std::abs(divergence_values[index]));
                }
        require(divergence_error < 1e-5f, "Divergence differs from backward differences of new velocity");
        if (std::strcmp(name, "velocity self-advection") == 0 || std::strcmp(name, "divergence stencil") == 0 || std::strcmp(name, "default resolution") == 0)
            require(magnitude > 0.01f, "Expected nonzero divergence");
        if (std::strcmp(name, "initial crossed flow") == 0)
            require(magnitude < 1e-6f, "Initial crossed flow is not divergence-free");
        if (std::strcmp(name, "divergence stencil") == 0) {
            // v=(x,2y,-0.5z) has divergence 2.5 away from the periodic seams.
            const auto index = geometry::cell_index(initial.density, 2, 2, 1);
            require(std::abs(divergence_values[index] - 2.5f) < 1e-5f, "Incorrect derivative scale or component");
        }

        if (std::strcmp(name, "velocity self-advection") == 0)
            require(velocity_change > 0.01f, "Velocity self-advection did not evolve the field");
        if (std::strcmp(name, "integer periodic shift") == 0) {
            // Both renderer descriptors must actually select their respective images.
            // A stale or hard-coded descriptor would produce identical pixels here.
            std::array<std::vector<float>, 2> pixels;
            for (uint32_t image = 0; image < 2; ++image) {
                auto& slot = c.slots[0]; begin(c, slot);
                graphics::record_volume_view(volume_views[image], slot.target, slot.command, {});
                submit(c, slot);
                pixels[image] = read_image(c, slot.target.color,
                    {slot.target.extent.width, slot.target.extent.height, 1}, 1,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            // RGBA8 bytes occupy the readback vector's storage; compare bit patterns.
            require(std::memcmp(pixels[0].data(), pixels[1].data(), pixels[0].size()*sizeof(float)) != 0,
                    "Renderer did not distinguish old and new density images");
        }
        if (std::strcmp(name, "integer periodic shift") == 0) {
            auto& slot = c.slots[0];
            vkDeviceWaitIdle(c.device);
            graphics::destroy_render_target(slot.target, c.device);
            graphics::create_render_target(slot.target,
                {pipeline.render_pass, pipeline.color_format, pipeline.depth_format},
                c.physical, c.device, {96, 96});
            // Look toward every face, exercising both signs of each box axis.
            const graphics::OrbitCamera cameras[] = {
                {0, 0}, {1.5707963f, 0}, {3.1415927f, 0}, {4.7123890f, 0},
                {0, 1.3f}, {0, -1.3f}};
            for (const auto& camera : cameras) {
                std::array<std::vector<float>, 3> images;
                for (unsigned grid = 0; grid < 3; ++grid) {
                    begin(c, slot);
                    graphics::record_volume_view(volume_views[0], slot.target, slot.command, camera, grid * 0.5f);
                    submit(c, slot);
                    images[grid] = read_image(c, slot.target.color, {96, 96, 1}, 1,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                const auto* plain = reinterpret_cast<const unsigned char*>(images[0].data());
                const auto* gridded = reinterpret_cast<const unsigned char*>(images[1].data());
                const auto* opaque = reinterpret_cast<const unsigned char*>(images[2].data());
                unsigned changed = 0, opacity_changed = 0;
                for (unsigned pixel = 0; pixel < 96 * 96; ++pixel) {
                    changed += std::memcmp(plain + 4 * pixel, gridded + 4 * pixel, 3) != 0;
                    opacity_changed += std::memcmp(gridded + 4 * pixel, opaque + 4 * pixel, 3) != 0;
                    require(gridded[4 * pixel + 3] == 255, "Domain grid changed output opacity");
                }
                require(changed > 50, "Domain face grid is missing from a camera direction");
                require(opacity_changed > 50, "Domain box opacity did not affect the rendered grid");
                require(std::memcmp(plain, gridded, 4) == 0,
                        "Domain grid leaked outside the box silhouette");
            }
        }
        const double mass = std::accumulate(actual.begin(), actual.end(), 0.0);
        const double initial_mass = std::accumulate(initial.density.values.begin(), initial.density.values.end(), 0.0);
        std::printf("%s: %u steps, max error %.3g, mass drift %.3g, velocity error %.3g\n", name, steps, error, mass-initial_mass, velocity_error);
        cleanup();
    } catch (...) { cleanup(); throw; }
}
}
int main() {
    Context c;
    try {
        if (!create(c)) { destroy(c); std::puts("No graphics/compute Vulkan device; skipped"); return 77; }
        auto defaults = simulation::initial_conditions();
        std::fill(defaults.temperature.begin(), defaults.temperature.end(), 3.0f);
        require(defaults.density.dimensions == std::array<uint32_t, 3>{64, 64, 64} &&
                defaults.density.spacing == 2.0f / 64, "Default grid and domain");
        // Tiny nonzero velocities near sin(pi) must not shift sampling by one cell.
        run(c, "odd crossed", simulation::initial_conditions({9,7,5},{},0.25f), 1, 0.04f);
        run(c, "initial crossed flow", defaults, 1, 0);
        run(c, "default resolution", defaults, 1, simulation::timestep);
        // Keep the long numerical regression at its explicit reference resolution.
        auto shear = simulation::initial_conditions({24, 24, 24}, {-1, -1, -1}, 2.0f / 24);
        for (uint32_t z = 0; z < 24; ++z)
            for (uint32_t y = 0; y < 24; ++y)
                for (uint32_t x = 0; x < 24; ++x)
                    shear.velocity[geometry::cell_index(shear.density, x, y, z)] = {0.5f * y / 23, 0, 0, 0};
        const auto& grid = shear.density;
        require(grid.values.front() == 0 && grid.values[23] == 1, "Density ramp endpoints");
        require(shear.velocity.front()[0] == 0 && shear.velocity[24*23][0] == 0.5f,
                "Velocity ramp endpoints");
        run(c, "linear shear", shear, 60, simulation::timestep);
        std::fill(shear.density.values.begin(), shear.density.values.end(), 0.5f);
        run(c, "uniform shear", shear, 600, simulation::timestep);
        auto fixture = simulation::initial_conditions({7, 5, 3}, {-2, 1, 4}, 0.25f);
        for (size_t i = 0; i < fixture.density.values.size(); ++i)
            fixture.density.values[i] = float((i * 17) % 101) / 100;
        for (size_t i = 0; i < fixture.temperature.size(); ++i) fixture.temperature[i] = float((i * 13) % 71) / 20;
        run(c, "zero timestep", fixture, 3, 0);
        for (auto& v : fixture.velocity) v = {};
        run(c, "zero velocity", fixture, 3, 0.25f);
        for (int axis = 0; axis < 3; ++axis) {
            for (auto& v : fixture.velocity) { v = {}; v[axis] = axis == 1 ? -1 : 1; }
            run(c, "integer periodic shift", fixture, 7, 0.25f);
        }
        for (auto& v : fixture.velocity) v = {0.3f, -0.7f, 0.2f, 0};
        run(c, "fractional transport", fixture, 11, 0.25f);
        auto smooth = simulation::initial_conditions({9, 7, 5}, {}, 0.25f);
        for (uint32_t z = 0; z < 5; ++z)
            for (uint32_t y = 0; y < 7; ++y)
                for (uint32_t x = 0; x < 9; ++x) {
                    const double r2 = std::pow((double(x)-4)/2, 2) + std::pow((double(y)-3)/2, 2) + std::pow(double(z)-2, 2);
                    smooth.density.values[geometry::cell_index(smooth.density, x, y, z)] = float(std::exp(-r2));
                }
        for (auto& v : smooth.velocity) v = {0.3f, -0.7f, 0.2f, 0};
        run(c, "smooth localized transport", smooth, 13, 0.25f);
        auto evolving = simulation::initial_conditions({9, 7, 5}, {}, 0.25f);
        for (uint32_t z = 0; z < 5; ++z)
            for (uint32_t y = 0; y < 7; ++y)
                for (uint32_t x = 0; x < 9; ++x) {
                    // Initially divergence-free periodic flow with nonzero self-advection.
                    const auto cell = geometry::cell_index(evolving.density, x, y, z);
                    evolving.velocity[cell] = {float(0.3 * std::sin(6.283185307 * y / 7)),
                        float(0.2 * std::cos(6.283185307 * z / 5)),
                        float(-0.4 * std::sin(6.283185307 * x / 9)), 0};
                }
        run(c, "velocity self-advection", evolving, 17, 0.1f);
        auto stencil = simulation::initial_conditions({7, 5, 3}, {}, 0.25f);
        for (uint32_t z = 0; z < 3; ++z)
            for (uint32_t y = 0; y < 5; ++y)
                for (uint32_t x = 0; x < 7; ++x)
                    stencil.velocity[geometry::cell_index(stencil.density, x, y, z)] =
                        {x * 0.25f, y * 0.5f, z * -0.125f, 0};
        run(c, "divergence stencil", stencil, 1, 0);
        auto thin = simulation::initial_conditions({1, 5, 7}, {}, 0.25f);
        thin.density.values[2] = 1;
        run(c, "singleton and variable velocity", thin, 9, 0.1f);
        destroy(c);
        require(c.errors == 0, "Vulkan validation errors");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); destroy(c); return 1; }
}
