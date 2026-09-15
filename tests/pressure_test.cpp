#include "vulkan_test_helpers.hpp"
#include "graphics/pressure.hpp"
#include <chrono>
#include <limits>
#include <numbers>

namespace {
std::vector<double> laplacian(const std::vector<float>& p, const geometry::UniformGrid& grid) {
    auto n = grid.dimensions;
    std::vector<double> result(p.size());
    for (uint32_t z = 0; z < n[2]; ++z)
        for (uint32_t y = 0; y < n[1]; ++y)
            for (uint32_t x = 0; x < n[0]; ++x) {
                uint32_t at[3] = {x,y,z};
                const auto i = geometry::cell_index(grid,x,y,z);
                double sum = 0;
                for (int a = 0; a < 3; ++a) {
                    auto original = at[a];
                    at[a] = (original + n[a] - 1) % n[a];
                    sum += p[geometry::cell_index(grid,at[0],at[1],at[2])];
                    at[a] = (original + 1) % n[a];
                    sum += p[geometry::cell_index(grid,at[0],at[1],at[2])];
                    at[a] = original;
                }
                result[i] = (sum - 6.0*p[i]) / (double(grid.spacing)*grid.spacing);
            }
    return result;
}
void verify(const simulation::PressureDiagnostics& diagnostic, const std::vector<float>& p,
            const std::vector<float>& divergence, const geometry::UniformGrid& grid, float dt,
            const simulation::PressureSettings& settings, bool must_converge = true) {
    require(diagnostic.finite, "Nonfinite pressure diagnostics");
    const auto lp = laplacian(p,grid);
    double mean_d = std::accumulate(divergence.begin(), divergence.end(), 0.0) / p.size();
    double mean_p = std::accumulate(p.begin(), p.end(), 0.0) / p.size();
    double r2 = 0, b2 = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        require(std::isfinite(p[i]), "Nonfinite pressure");
        double b = dt > 0 ? (divergence[i] - mean_d) / dt : 0;
        r2 += (b-lp[i])*(b-lp[i]); b2 += b*b;
    }
    double residual = std::sqrt(r2 / p.size()), rhs = std::sqrt(b2 / p.size());
    require(std::abs(mean_p) < 1e-5, "Pressure gauge is not zero mean");
    require(std::abs(diagnostic.residual_rms - residual) < 1e-4 + rhs*1e-4, "GPU residual disagrees with independent stencil");
    require(std::abs(diagnostic.rhs_rms - rhs) < 1e-4 + rhs*1e-4, "GPU RHS norm is wrong");
    if (must_converge) {
        require(simulation::pressure_converged(diagnostic,settings), "Pressure missed GPU residual tolerance");
        require(residual <= settings.absolute_tolerance + settings.relative_tolerance*rhs,
            "Pressure missed independent residual tolerance");
    }
    std::printf("  residual %.6g, RHS %.6g, mean %.3g\n", residual, rhs, mean_p);
}
void manufactured(Context& c, std::array<uint32_t,3> dimensions, unsigned iterations,
                  float spacing, float dt, int mode = 0) {
    auto initial = simulation::initial_conditions(dimensions, {}, spacing);
    const auto& grid = initial.density;
    VkExtent3D extent{dimensions[0],dimensions[1],dimensions[2]};
    simulation::SimulationFields fields;
    simulation::PressureSolve solve;
    auto cleanup = [&] { vkDeviceWaitIdle(c.device); simulation::destroy_pressure(solve,c.device); simulation::destroy_fields(fields,c.device); };
    try {
        simulation::create_fields(fields,c.physical,c.device,c.queue,c.family,initial);
        simulation::create_pressure(solve,c.physical,c.device,simulation::divergence_view(fields,0),
            {simulation::pressure_view(fields,0,0),simulation::pressure_view(fields,1,0)},2);
        std::vector<float> expected(grid.values.size(),0);
        for (uint32_t z = 0; z < dimensions[2]; ++z)
            for (uint32_t y = 0; y < dimensions[1]; ++y)
                for (uint32_t x = 0; x < dimensions[0]; ++x) {
                    double phase = 2*std::numbers::pi*(x+0.5)/dimensions[0];
                    double value = dimensions[0] > 1 ? std::sin(phase) : std::cos(2*std::numbers::pi*(y+0.5)/dimensions[1]);
                    if (mode == 1) value = ((x+y+z)%2) ? -0.25 : 0.25;
                    if (mode == 2 || mode == 3) value = 0;
                    expected[geometry::cell_index(grid,x,y,z)] = float(value);
                }
        auto lp = laplacian(expected,grid);
        std::vector<float> d(expected.size());
        // Keep d independent of the solve's dt to test inverse-dt scaling.
        constexpr float reference_dt = 0.1f;
        for (size_t i = 0; i < d.size(); ++i) d[i] = float(reference_dt*lp[i] + (mode == 3 ? 2.0 : 0.125));
        if (mode == 2) std::fill(d.begin(),d.end(),0);
        gpu::upload_image(fields.divergence,c.physical,c.device,c.queue,c.family,extent,d.data(),d.size()*4,VK_IMAGE_LAYOUT_GENERAL);
        simulation::PressureSettings settings; settings.iterations = iterations;
        auto& slot = c.slots[0]; begin(c,slot);
        // Reject invalid inputs before any dispatches are recorded.
        for (float invalid : {-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            bool rejected = false;
            try { simulation::record_pressure(solve,slot.command,invalid,1,0,settings); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected,"Invalid pressure timestep accepted");
        }
        auto output = simulation::record_pressure(solve,slot.command,dt,7,0,settings);
        require(output.image.image == fields.pressure[dt > 0 ? iterations%2 : 0].image && output.version == 7,
            "Wrong final pressure index or version");
        submit(c,slot);
        auto p = read_image(c,output.image,extent,1,VK_IMAGE_LAYOUT_GENERAL);
        auto diagnostic = simulation::read_pressure_diagnostics(solve,c.device,0);
        std::printf("manufactured %ux%ux%u, %u iterations, dt %.3g, mode %d\n",dimensions[0],dimensions[1],dimensions[2],iterations,dt,mode);
        verify(diagnostic,p,d,grid,dt,settings);
        double error = 0;
        for (size_t i = 0; i < p.size(); ++i)
            error = std::max(error,std::abs(double(p[i])-(dt > 0 ? expected[i]*reference_dt/dt : 0)));
        require(error < 0.002,"Manufactured pressure sign or scale incorrect");
        auto unchanged = read_image(c,fields.divergence,extent,1,VK_IMAGE_LAYOUT_GENERAL);
        require(unchanged == d,"Pressure modified divergence");
        // Insufficient iteration budgets must be distinguishable from convergence.
        if (mode == 0 && dt > 0) {
            begin(c,slot); settings.iterations = 1;
            simulation::record_pressure(solve,slot.command,dt,8,0,settings); submit(c,slot);
            check(vkWaitForFences(c.device,1,&slot.fence,VK_TRUE,UINT64_MAX));
            require(!simulation::pressure_converged(simulation::read_pressure_diagnostics(solve,c.device,0),settings),
                "Unconverged solve reported success");
        }
        if (mode == 0 && dt > 0) {
            auto density = read_image(c,fields.density[0],extent,1,VK_IMAGE_LAYOUT_GENERAL);
            auto velocity = read_image(c,fields.velocity[0],extent,4,VK_IMAGE_LAYOUT_GENERAL);
            require(density == initial.density.values,"Pressure modified density");
            require(std::memcmp(velocity.data(),initial.velocity.data(),velocity.size()*sizeof(float)) == 0,
                "Pressure modified velocity");
            begin(c,slot);
            auto zero = simulation::record_pressure(solve,slot.command,0,9,0,settings);
            submit(c,slot);
            auto cleared = read_image(c,zero.image,extent,1,VK_IMAGE_LAYOUT_GENERAL);
            require(std::all_of(cleared.begin(),cleared.end(),[](float value) { return value == 0; }),
                "Zero timestep retained stale pressure");
            require(simulation::pressure_converged(simulation::read_pressure_diagnostics(solve,c.device,0),settings),
                "Zero timestep retained stale diagnostics");
            d[0] = std::numeric_limits<float>::quiet_NaN();
            gpu::upload_image(fields.divergence,c.physical,c.device,c.queue,c.family,extent,d.data(),d.size()*4,VK_IMAGE_LAYOUT_GENERAL);
            begin(c,slot);
            simulation::record_pressure(solve,slot.command,dt,10,0,settings); submit(c,slot);
            check(vkWaitForFences(c.device,1,&slot.fence,VK_TRUE,UINT64_MAX));
            require(!simulation::read_pressure_diagnostics(solve,c.device,0).finite,"Nonfinite RHS was not reported");
        }
        cleanup();
    } catch (...) { cleanup(); throw; }
}

void evolving(Context& c, uint32_t size, uint32_t steps, bool solve_each_step) {
    auto initial = simulation::initial_conditions({size,size,size},{-1,-1,-1},2.f/size);
    const auto& grid = initial.density;
    VkExtent3D extent{size,size,size};
    simulation::SimulationFields fields;
    simulation::Advection advection;
    simulation::Divergence divergence;
    simulation::PressureSolve pressure;
    graphics::VolumePipeline pipeline;
    std::array<graphics::VolumeView,2> pressure_views{}, density_views{};
    VkQueryPool timestamps{};
    VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(c.physical,&properties);
    uint32_t family_count{}; vkGetPhysicalDeviceQueueFamilyProperties(c.physical,&family_count,nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(c.physical,&family_count,families.data());
    const auto bits = families[c.family].timestampValidBits;
    auto cleanup = [&] {
        vkDeviceWaitIdle(c.device);
        if (timestamps) vkDestroyQueryPool(c.device,timestamps,nullptr);
        for (auto& slot : c.slots) graphics::destroy_render_target(slot.target,c.device);
        for (auto& view : pressure_views) graphics::destroy_volume_view(view,c.device);
        for (auto& view : density_views) graphics::destroy_volume_view(view,c.device);
        graphics::destroy_volume_pipeline(pipeline,c.device);
        simulation::destroy_pressure(pressure,c.device);
        simulation::destroy_divergence(divergence,c.device);
        simulation::destroy_advection(advection,c.device);
        simulation::destroy_fields(fields,c.device);
    };
    try {
        simulation::create_fields(fields,c.physical,c.device,c.queue,c.family,initial);
        simulation::create_advection(advection,c.physical,c.device,
            {simulation::density_view(fields,0,0),simulation::density_view(fields,1,0)},
            {simulation::velocity_view(fields,0,0),simulation::velocity_view(fields,1,0)});
        simulation::create_divergence(divergence,c.physical,c.device,
            {simulation::velocity_view(fields,0,0),simulation::velocity_view(fields,1,0)},simulation::divergence_view(fields,0));
        simulation::create_pressure(pressure,c.physical,c.device,simulation::divergence_view(fields,0),
            {simulation::pressure_view(fields,0,0),simulation::pressure_view(fields,1,0)},2);
        graphics::create_volume_pipeline(pipeline,c.physical,c.device);
        for (unsigned i = 0; i < 2; ++i) {
            graphics::create_volume_view(pressure_views[i],c.device,pipeline,simulation::pressure_view(fields,i,0),true,0.25f);
            graphics::create_volume_view(density_views[i],c.device,pipeline,simulation::density_view(fields,i,0));
            graphics::create_render_target(c.slots[i].target,{pipeline.render_pass,pipeline.color_format,pipeline.depth_format},
                c.physical,c.device,{16,16});
        }
        if (bits) {
            VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = 2;
            check(vkCreateQueryPool(c.device,&info,nullptr,&timestamps));
        }
        graphics::FieldView output;
        simulation::PressureSettings settings;
        if (size < 64) settings.iterations = 512;
        for (uint32_t step_index = 0; step_index < steps; ++step_index) {
            auto& slot = c.slots[step_index%2]; begin(c,slot);
            auto step = simulation::prepare_advection(fields);
            simulation::record_advection(advection,slot.command,step);
            simulation::record_divergence(divergence,slot.command,step.velocity_output);
            if (solve_each_step || step_index == steps-1) {
                // Exercise both pressure directions independently of velocity parity.
                settings.iterations = (size < 64 ? 512u : simulation::PressureSettings{}.iterations) + step_index % 2;
                if (timestamps) {
                    // Query reuse must follow previous bottom-of-pipe writes.
                    vkCmdPipelineBarrier(slot.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,0,nullptr);
                    vkCmdResetQueryPool(slot.command,timestamps,0,2);
                    vkCmdWriteTimestamp(slot.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timestamps,0);
                }
                output = simulation::record_pressure(pressure,slot.command,step.dt,step.output.version,step_index%2,settings);
                if (timestamps) vkCmdWriteTimestamp(slot.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamps,1);
                graphics::record_volume_view(pressure_views[settings.iterations%2],slot.target,slot.command,{});
            }
            graphics::record_volume_view(density_views[(step_index+1)%2],slot.target,slot.command,{});
            submit(c,slot); simulation::commit_step(fields,step);
        }
        check(vkQueueWaitIdle(c.queue));
        if (timestamps) {
            uint64_t ticks[2]{};
            check(vkGetQueryPoolResults(c.device,timestamps,0,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT));
            const uint64_t mask = bits == 64 ? UINT64_MAX : (uint64_t(1)<<bits)-1;
            std::printf("%u cubed step %u: %u iterations, GPU pressure %.3f ms\n",size,steps,settings.iterations,
                ((ticks[1]-ticks[0])&mask)*double(properties.limits.timestampPeriod)/1e6);
        }
        auto p = read_image(c,output.image,extent,1,VK_IMAGE_LAYOUT_GENERAL);
        auto d = read_image(c,fields.divergence,extent,1,VK_IMAGE_LAYOUT_GENERAL);
        auto v = read_image(c,simulation::current_velocity(fields).image,extent,4,VK_IMAGE_LAYOUT_GENERAL);
        double max_d_error = 0;
        for (uint32_t z = 0; z < size; ++z)
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x) {
                    uint32_t at[3]={x,y,z}; double expected = 0;
                    for (uint32_t a=0;a<3;++a) {
                        auto old = at[a]; at[a]=(old+1)%size;
                        expected += v[4*geometry::cell_index(grid,at[0],at[1],at[2])+a];
                        at[a]=(old+size-1)%size;
                        expected -= v[4*geometry::cell_index(grid,at[0],at[1],at[2])+a]; at[a]=old;
                    }
                    expected /= 2*grid.spacing;
                    max_d_error=std::max(max_d_error,std::abs(expected-d[geometry::cell_index(grid,x,y,z)]));
                }
        require(max_d_error < 1e-5,"Pressure RHS is not from the advected velocity");
        const auto diagnostic = simulation::read_pressure_diagnostics(pressure,c.device,(steps-1)%2);
        // Small-grid acceptance solves retain their explicit accuracy budget.
        // The interactive default may stop early, but must reduce the residual.
        verify(diagnostic,p,d,grid,simulation::timestep,settings,size < 64);
        require(diagnostic.residual_rms < diagnostic.rhs_rms, "Pressure did not reduce the initial residual");
        float magnitude = 0; for (auto value : p) magnitude=std::max(magnitude,std::abs(value));
        std::printf("  max |pressure| %.6g\n",magnitude);
        // Same target/camera, distinct field bindings must produce distinct pixels.
        std::array<std::vector<float>,2> pixels;
        auto& slot = c.slots[0];
        for (unsigned i=0;i<2;++i) {
            begin(c,slot);
            graphics::record_volume_view(i ? density_views[steps%2] : pressure_views[settings.iterations%2],slot.target,slot.command,{});
            submit(c,slot);
            pixels[i]=read_image(c,slot.target.color,{16,16,1},1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        require(std::memcmp(pixels[0].data(),pixels[1].data(),pixels[0].size()*4)!=0,"Density and pressure rendering are identical");
        cleanup();
    } catch (...) { cleanup(); throw; }
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    Context c;
    try {
        if (!create(c)) { destroy(c); return 77; }
        manufactured(c,{9,7,5},601,0.25f,0.1f);
        manufactured(c,{9,7,5},600,0.5f,0.2f);
        manufactured(c,{1,5,2},300,0.25f,0.1f);
        manufactured(c,{8,8,8},15,0.25f,0.1f,1);
        manufactured(c,{3,3,3},20,0.25f,0.1f,2);
        manufactured(c,{3,3,3},20,0.25f,0.1f,3);
        manufactured(c,{3,3,3},20,0.25f,0);
        evolving(c,16,4,true);
        evolving(c,64,1,false);
        evolving(c,64,60,false);
        destroy(c); require(c.errors == 0,"Vulkan validation errors");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); destroy(c); return 1; }
}
