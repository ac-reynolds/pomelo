#include "vulkan_test_helpers.hpp"
#include "graphics/smoke.hpp"
#include "graphics/pressure.hpp"
#include "graphics/projection.hpp"
#include <limits>
#include <numbers>

namespace {
using Dimensions = std::array<uint32_t,3>;
size_t index(Dimensions n, uint32_t x, uint32_t y, uint32_t z) {
    return x + size_t(n[0]) * (y + size_t(n[1])*z);
}
size_t neighbor(Dimensions n, size_t i, unsigned axis, int offset) {
    uint32_t at[3] = {uint32_t(i%n[0]),uint32_t(i/n[0]%n[1]),uint32_t(i/(size_t(n[0])*n[1]))};
    at[axis] = uint32_t((int(at[axis])+int(n[axis])+offset)%int(n[axis]));
    return index(n,at[0],at[1],at[2]);
}
std::vector<double> divergence_of(const std::vector<float>& velocity, Dimensions n, double h) {
    std::vector<double> d(velocity.size()/4);
    for (size_t i=0;i<d.size();++i)
        for (unsigned a=0;a<3;++a)
            d[i] += (double(velocity[4*i+a])-velocity[4*neighbor(n,i,a,-1)+a])/h;
    return d;
}
double rms(const std::vector<double>& values) {
    double sum=0; for (double v : values) sum+=v*v;
    return std::sqrt(sum/values.size());
}
void compare(const std::vector<float>& actual, const std::vector<float>& expected, float tolerance, const char* message) {
    require(actual.size()==expected.size(),"Reference size mismatch");
    float error=0;
    for (size_t i=0;i<actual.size();++i) {
        require(std::isfinite(actual[i]),message);
        error=std::max(error,std::abs(actual[i]-expected[i]));
    }
    if (error>tolerance) std::fprintf(stderr,"%s: max error %.9g, tolerance %.9g\n",message,error,tolerance);
    require(error<=tolerance,message);
}

void exact_gradient(Context& c, Dimensions n, float h, float dt, bool constant_pressure = false) {
    auto initial=simulation::initial_conditions(n,{},h);
    VkExtent3D extent{n[0],n[1],n[2]};
    simulation::SimulationFields fields;
    simulation::Projection projection;
    auto cleanup=[&] {
        vkDeviceWaitIdle(c.device); simulation::destroy_projection(projection,c.device); simulation::destroy_fields(fields,c.device);
    };
    try {
        simulation::create_fields(fields,c.physical,c.device,c.queue,c.family,initial);
        simulation::create_projection(projection,c.physical,c.device,
            {simulation::velocity_view(fields,0,0),simulation::velocity_view(fields,1,0)},
            {simulation::pressure_view(fields,0,0),simulation::pressure_view(fields,1,0)});
        const size_t cells=initial.density.values.size();
        std::array<std::vector<float>,2> pressures;
        for (unsigned p=0;p<2;++p) {
            pressures[p].resize(cells);
            for (size_t i=0;i<cells;++i)
                pressures[p][i]=constant_pressure ? 3.f : float((p+1)*0.1*std::sin(i*0.73)+(i%2 ? 0.03 : -0.03));
            gpu::upload_image(fields.pressure[p],c.physical,c.device,c.queue,c.family,extent,
                pressures[p].data(),cells*sizeof(float),VK_IMAGE_LAYOUT_GENERAL);
        }
        for (unsigned v=0;v<2;++v) for (unsigned p=0;p<2;++p) {
            std::vector<float> input(cells*4),expected(cells*4);
            for (size_t i=0;i<cells;++i) for (unsigned a=0;a<3;++a) {
                float background=(v+1)*(a+1)*0.125f;
                float change=(dt > 0 ? dt : 0.1f)/h*(pressures[p][neighbor(n,i,a,1)]-pressures[p][i]);
                input[4*i+a]=background+change;
                expected[4*i+a]=dt==0 ? input[4*i+a] : background;
            }
            gpu::upload_image(fields.velocity[v],c.physical,c.device,c.queue,c.family,extent,
                input.data(),input.size()*sizeof(float),VK_IMAGE_LAYOUT_GENERAL);
            auto& slot=c.slots[(v*2+p)%2]; begin(c,slot);
            auto velocity=simulation::velocity_view(fields,v,3), pressure=simulation::pressure_view(fields,p,3);
            for (float invalid : {-0.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                bool rejected=false;
                try { simulation::record_projection(projection,slot.command,velocity,pressure,invalid); }
                catch (const std::invalid_argument&) { rejected=true; }
                require(rejected,"Invalid projection dt accepted");
            }
            auto stale=pressure; stale.version=2;
            bool rejected=false;
            try { simulation::record_projection(projection,slot.command,velocity,stale,dt); }
            catch (const std::invalid_argument&) { rejected=true; }
            require(rejected,"Stale pressure accepted");
            simulation::record_projection(projection,slot.command,velocity,pressure,dt); submit(c,slot);
            const auto actual=read_image(c,velocity.image,extent,4,VK_IMAGE_LAYOUT_GENERAL);
            compare(actual,expected,1e-6f,"Incorrect projection sign, scale, or immutable descriptor");
            if (dt>0) require(rms(divergence_of(actual,n,h))<1e-5,"Exact gradient did not project to zero divergence");
            compare(read_image(c,pressure.image,extent,1,VK_IMAGE_LAYOUT_GENERAL),pressures[p],0,"Projection changed pressure");
        }
        compare(read_image(c,fields.density[0],extent,1,VK_IMAGE_LAYOUT_GENERAL),initial.density.values,0,
            "Projection changed density");
        std::printf("exact gradient %ux%ux%u, h %.3g, dt %.3g, constant %d: all four bindings passed\n",
            n[0],n[1],n[2],h,dt,constant_pressure);
        cleanup();
    } catch (...) { cleanup(); throw; }
}

// Independent CPU simulation reference: periodic trilinear transport, a scalar
// weighted Jacobi solve in double precision, then the compatible gradient.
std::vector<float> advect_reference(const std::vector<float>& values, unsigned components,
    const std::vector<float>& velocity, Dimensions n, double dt_over_h) {
    std::vector<float> result(values.size());
    for (uint32_t z=0;z<n[2];++z) for (uint32_t y=0;y<n[1];++y) for (uint32_t x=0;x<n[0];++x) {
        const size_t i=index(n,x,y,z);
        const double departure[3]={x-dt_over_h*velocity[4*i],y-dt_over_h*velocity[4*i+1],z-dt_over_h*velocity[4*i+2]};
        for (unsigned component=0;component<components;++component) {
            double value=0;
            for (unsigned corner=0;corner<8;++corner) {
                uint32_t at[3]; double weight=1;
                for (unsigned axis=0;axis<3;++axis) {
                    const auto low=std::floor(departure[axis]);
                    const double fraction=departure[axis]-low;
                    const bool high=(corner>>axis)&1;
                    weight*=high ? fraction : 1-fraction;
                    int coordinate=(int(low)+int(high))%int(n[axis]);
                    if (coordinate<0) coordinate+=int(n[axis]);
                    at[axis]=uint32_t(coordinate);
                }
                value+=weight*values[components*index(n,at[0],at[1],at[2])+component];
            }
            result[components*i+component]=float(value);
        }
    }
    return result;
}
std::vector<float> pressure_reference(const std::vector<double>& d, Dimensions n, double h, double dt, uint32_t iterations) {
    double mean=std::accumulate(d.begin(),d.end(),0.0)/d.size();
    std::vector<double> p(d.size()),next(d.size());
    for (uint32_t iteration=0;iteration<iterations;++iteration) {
        for (size_t i=0;i<p.size();++i) {
            double sum=0;
            for (unsigned a=0;a<3;++a) sum+=p[neighbor(n,i,a,-1)]+p[neighbor(n,i,a,1)];
            next[i]=p[i]/3+(2.0/3)*(sum-h*h*(d[i]-mean)/dt)/6;
        }
        p.swap(next);
    }
    mean=std::accumulate(p.begin(),p.end(),0.0)/p.size();
    std::vector<float> result(p.size());
    for (size_t i=0;i<p.size();++i) result[i]=float(p[i]-mean);
    return result;
}

void source_updates(Context& c) {
    const Dimensions n{9,11,7};
    const VkExtent3D extent{n[0],n[1],n[2]};
    auto initial=simulation::smoke_initial_conditions(n,{-2,1,4},0.25f);
    std::fill(initial.density.values.begin(),initial.density.values.end(),0.8f);
    std::fill(initial.temperature.begin(),initial.temperature.end(),2.0f);
    for (auto& v:initial.velocity) v={0.3f,-0.2f,0.1f,0};
    simulation::SimulationFields fields;
    simulation::Smoke source;
    auto cleanup=[&] {
        vkDeviceWaitIdle(c.device);
        simulation::destroy_smoke(source,c.device);
        simulation::destroy_fields(fields,c.device);
    };
    try {
        simulation::create_fields(fields,c.physical,c.device,c.queue,c.family,initial);
        simulation::create_smoke(source,c.physical,c.device,fields);
        auto p=simulation::smoke_parameters(fields.domain);
        p.density_rate=20;
        for (unsigned mode=0;mode<4;++mode) {
            const float dt=mode==0 ? 0 : 0.2f;
            p.enabled=mode==1;
            const float initial_heat=mode==3 ? 0.0f : 2.0f;
            std::fill(initial.temperature.begin(),initial.temperature.end(),initial_heat);
            // Reset both destinations so every case has independently known input.
            for (unsigned i=0;i<2;++i) {
                gpu::upload_image(fields.density[i],c.physical,c.device,c.queue,c.family,extent,
                    initial.density.values.data(),initial.density.values.size()*sizeof(float),VK_IMAGE_LAYOUT_GENERAL);
                gpu::upload_image(fields.temperature[i],c.physical,c.device,c.queue,c.family,extent,
                    initial.temperature.data(),initial.temperature.size()*sizeof(float),VK_IMAGE_LAYOUT_GENERAL);
                gpu::upload_image(fields.velocity[i],c.physical,c.device,c.queue,c.family,extent,
                    initial.velocity.data(),initial.velocity.size()*sizeof(initial.velocity[0]),VK_IMAGE_LAYOUT_GENERAL);
            }
            auto step=simulation::prepare_advection(fields,dt);
            auto& slot=c.slots[mode%2]; begin(c,slot);
            if (mode==0) {
                auto invalid=p; invalid.radius=0;
                bool rejected=false;
                try { simulation::record_smoke(source,slot.command,step,invalid); }
                catch (const std::invalid_argument&) { rejected=true; }
                require(rejected,"Invalid emitter radius accepted");
            }
            simulation::record_smoke(source,slot.command,step,p);
            submit(c,slot); simulation::commit_step(fields,step);
            auto rho=read_image(c,simulation::current_density(fields).image,extent,1,VK_IMAGE_LAYOUT_GENERAL);
            auto theta=read_image(c,simulation::current_temperature(fields).image,extent,1,VK_IMAGE_LAYOUT_GENERAL);
            auto velocity=read_image(c,simulation::current_velocity(fields).image,extent,4,VK_IMAGE_LAYOUT_GENERAL);
            bool saturated=false, interior=false, exterior=false;
            for (uint32_t z=0;z<n[2];++z) for (uint32_t y=0;y<n[1];++y) for (uint32_t x=0;x<n[0];++x) {
                const auto i=geometry::cell_index(initial.density,x,y,z);
                const auto pos=geometry::cell_center(initial.density,x,y,z);
                double r2=0;
                for (unsigned a=0;a<3;++a) r2+=std::pow((pos[a]-p.center[a])/p.radius,2);
                interior|=r2<1; exterior|=r2>=1;
                double w=p.enabled ? std::pow(std::max(0.0,1-r2),2) : 0;
                const double expected_rho=std::min(1.0,0.8f*std::exp(-double(p.dissipation)*dt)+dt*p.density_rate*w);
                const double expected_theta=initial_heat*std::exp(-double(p.cooling)*dt)+dt*p.heat_rate*w;
                saturated|=expected_rho==1;
                require(std::abs(rho[i]-expected_rho)<2e-6,"Source density incorrect");
                require(std::abs(theta[i]-expected_theta)<2e-6,"Source heat/cooling incorrect");
                require(std::abs(velocity[4*i+1]-(-0.2f+dt*p.buoyancy*expected_theta))<2e-6,"Source buoyancy incorrect");
                require(velocity[4*i]==0.3f && velocity[4*i+2]==0.1f && velocity[4*i+3]==0,"Source changed other velocity components");
            }
            require(interior && exterior,"Source coverage incomplete");
            if (dt>0 && p.enabled) require(saturated,"Source saturation not exercised");
        }
        cleanup();
    } catch (...) { cleanup(); throw; }
}

void full_steps(Context& c, Dimensions n, unsigned steps, bool smoke = false) {
    constexpr float h=0.25f,dt=0.04f;
    auto initial=smoke ? simulation::smoke_initial_conditions(n,{-2,1,4},h) : simulation::initial_conditions(n,{},h);
    VkExtent3D extent{n[0],n[1],n[2]};
    simulation::SimulationFields fields;
    simulation::Advection advection;
    simulation::Smoke source;
    simulation::SmokeParameters source_parameters;
    simulation::Divergence divergence;
    simulation::PressureSolve pressure;
    simulation::Projection projection;
    graphics::VolumePipeline pipeline;
    std::array<graphics::VolumeView,2> pressure_views{},density_views{};
    auto cleanup=[&] {
        vkDeviceWaitIdle(c.device);
        for (auto& slot:c.slots) graphics::destroy_render_target(slot.target,c.device);
        for (auto& view:pressure_views) graphics::destroy_volume_view(view,c.device);
        for (auto& view:density_views) graphics::destroy_volume_view(view,c.device);
        graphics::destroy_volume_pipeline(pipeline,c.device);
        simulation::destroy_projection(projection,c.device);
        simulation::destroy_pressure(pressure,c.device);
        simulation::destroy_divergence(divergence,c.device);
        simulation::destroy_smoke(source,c.device);
        simulation::destroy_advection(advection,c.device);
        simulation::destroy_fields(fields,c.device);
    };
    try {
        simulation::create_fields(fields,c.physical,c.device,c.queue,c.family,initial);
        const std::array v{simulation::velocity_view(fields,0,0),simulation::velocity_view(fields,1,0)};
        const std::array d{simulation::density_view(fields,0,0),simulation::density_view(fields,1,0)};
        const std::array p{simulation::pressure_view(fields,0,0),simulation::pressure_view(fields,1,0)};
        simulation::create_advection(advection,c.physical,c.device,d,v,
            {simulation::temperature_view(fields, 0, 0), simulation::temperature_view(fields, 1, 0)});
        simulation::create_divergence(divergence,c.physical,c.device,v,simulation::divergence_view(fields,0));
        simulation::create_pressure(pressure,c.physical,c.device,simulation::divergence_view(fields,0),p,2);
        simulation::create_projection(projection,c.physical,c.device,v,p);
        if (smoke) {
            simulation::create_smoke(source,c.physical,c.device,fields);
            source_parameters = simulation::smoke_parameters(fields.domain);
        }
        graphics::create_volume_pipeline(pipeline,c.physical,c.device);
        for (unsigned i=0;i<2;++i) {
            graphics::create_volume_view(pressure_views[i],c.device,pipeline,p[i],true,0.25f);
            graphics::create_volume_view(density_views[i],c.device,pipeline,d[i]);
            graphics::create_render_target(c.slots[i].target,{pipeline.render_pass,pipeline.color_format,pipeline.depth_format},
                c.physical,c.device,{16,16});
        }
        auto expected_density=initial.density.values;
        auto expected_temperature=initial.temperature;
        std::vector<float> expected_velocity(initial.velocity.size()*4);
        std::memcpy(expected_velocity.data(),initial.velocity.data(),expected_velocity.size()*sizeof(float));
        std::vector<float> expected_pressure;
        std::vector<double> before;
        graphics::FieldView final_pressure;
        for (unsigned step_index=0;step_index<steps;++step_index) {
            auto& slot=c.slots[step_index%2]; begin(c,slot);
            auto step=simulation::prepare_advection(fields,dt);
            simulation::record_advection(advection,slot.command,step);
            if (smoke) {
                source_parameters.enabled = step_index < steps / 2;
                simulation::record_smoke(source,slot.command,step,source_parameters);
            }
            simulation::record_divergence(divergence,slot.command,step.velocity_output);
            simulation::PressureSettings settings;
            settings.iterations+=(step_index/2)%2;
            final_pressure=simulation::record_pressure(pressure,slot.command,dt,step.output.version,step_index%2,settings);
            simulation::record_projection(projection,slot.command,step.velocity_output,final_pressure,dt);
            simulation::record_divergence(divergence,slot.command,step.velocity_output);
            // Both scalar selections exercise the pressure/velocity reuse barriers.
            graphics::record_volume_view(pressure_views[settings.iterations%2],slot.target,slot.command,{});
            graphics::record_volume_view(density_views[(step_index+1)%2],slot.target,slot.command,{});
            submit(c,slot); simulation::commit_step(fields,step);

            expected_temperature=advect_reference(expected_temperature,1,expected_velocity,n,double(dt)/h);
            expected_density=advect_reference(expected_density,1,expected_velocity,n,double(dt)/h);
            expected_velocity=advect_reference(expected_velocity,4,expected_velocity,n,double(dt)/h);
            if (smoke) {
                const auto& p = source_parameters;
                for (uint32_t z=0;z<n[2];++z) for (uint32_t y=0;y<n[1];++y) for (uint32_t x=0;x<n[0];++x) {
                    const auto i=geometry::cell_index(initial.density,x,y,z);
                    const auto pos=geometry::cell_center(initial.density,x,y,z);
                    double r2=0;
                    for (unsigned a=0;a<3;++a) r2+=std::pow((pos[a]-p.center[a])/p.radius,2);
                    const double w=p.enabled ? std::pow(std::max(0.0,1-r2),2) : 0;
                    expected_density[i]=std::min(1.0,expected_density[i]*std::exp(-double(p.dissipation)*dt)+dt*p.density_rate*w);
                    expected_temperature[i]=expected_temperature[i]*std::exp(-double(p.cooling)*dt)+dt*p.heat_rate*w;
                    expected_velocity[4*i+1]+=dt*p.buoyancy*expected_temperature[i];
                }
            }
            before=divergence_of(expected_velocity,n,h);
            expected_pressure=pressure_reference(before,n,h,dt,settings.iterations);
            for (size_t i=0;i<before.size();++i) for (unsigned a=0;a<3;++a)
                expected_velocity[4*i+a]-=dt/h*(expected_pressure[neighbor(n,i,a,1)]-expected_pressure[i]);
        }
        compare(read_image(c,simulation::current_temperature(fields).image,extent,1,VK_IMAGE_LAYOUT_GENERAL),expected_temperature,2e-5f,
            "Temperature did not follow the projected flow and source updates");
        if (smoke) {
            double heat=0, height=0;
            for (uint32_t z=0;z<n[2];++z) for (uint32_t y=0;y<n[1];++y) for (uint32_t x=0;x<n[0];++x) {
                const auto i=geometry::cell_index(initial.density,x,y,z);
                heat+=expected_temperature[i];
                height+=expected_temperature[i]*geometry::cell_center(initial.density,x,y,z)[1];
            }
            require(heat>0 && height/heat>source_parameters.center[1]+0.005,"Heat did not rise away from emitter");
        }
        auto actual_velocity=read_image(c,simulation::current_velocity(fields).image,extent,4,VK_IMAGE_LAYOUT_GENERAL);
        compare(actual_velocity,expected_velocity,2e-5f,"Projected velocity or next-step feedback differs from CPU reference");
        compare(read_image(c,simulation::current_density(fields).image,extent,1,VK_IMAGE_LAYOUT_GENERAL),expected_density,2e-5f,
            "Density did not advect with corrected velocity");
        compare(read_image(c,final_pressure.image,extent,1,VK_IMAGE_LAYOUT_GENERAL),expected_pressure,2e-4f,
            "End-to-end pressure differs from CPU reference");
        auto final_divergence=read_image(c,fields.divergence,extent,1,VK_IMAGE_LAYOUT_GENERAL);
        auto after=divergence_of(actual_velocity,n,h);
        for (size_t i=0;i<after.size();++i)
            require(std::abs(final_divergence[i]-after[i])<1e-5,"Divergence image does not describe corrected velocity");
        const auto diagnostics=simulation::read_pressure_diagnostics(pressure,c.device,(steps-1)%2);
        require(diagnostics.finite,"Nonfinite projection diagnostics");
        require(std::abs(rms(after)-dt*diagnostics.residual_rms)<1e-5,
            "Projection divergence is not the scaled Poisson residual");
        require(rms(after)<=std::max(1e-6,rms(before)*0.25),"Projection did not substantially reduce divergence");
        std::printf("%ux%ux%u, %u full steps: RMS divergence %.6g -> %.6g (64/65 Jacobi iterations)\n",
            n[0],n[1],n[2],steps,rms(before),rms(after));
        cleanup();
    } catch (...) { cleanup(); throw; }
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    Context c;
    try {
        if (!create(c)) { destroy(c); return 77; }
        exact_gradient(c,{7,5,3},0.25f,0.1f);
        exact_gradient(c,{8,6,4},0.5f,0.025f);
        exact_gradient(c,{1,5,2},0.125f,0.1f);
        exact_gradient(c,{1,1,1},0.25f,0.1f);
        exact_gradient(c,{7,5,3},0.25f,0);
        exact_gradient(c,{7,5,3},0.25f,0.1f,true);
        full_steps(c,{8,6,4},8);
        full_steps(c,{9,7,5},7);
        full_steps(c,{1,5,2},4);
        source_updates(c);
        full_steps(c,{9,11,7},60,true);
        destroy(c); require(c.errors==0,"Vulkan validation errors"); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); destroy(c); return 1; }
}
