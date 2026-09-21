struct Parameters {
    uint3 dimensions;
    float dt;
    float3 origin;
    float spacing;
    float3 center;
    float radius;
    float density_rate;
    float heat_rate;
    float cooling;
    float dissipation;
    float buoyancy;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[[vk::binding(0, 0)]] RWTexture3D<float> density;
[[vk::binding(1, 0)]] RWTexture3D<float4> velocity;
[[vk::binding(2, 0)]] RWTexture3D<float> temperature;

[numthreads(4, 4, 4)]
void main(uint3 i : SV_DispatchThreadID) {
    if (any(i >= parameters.dimensions)) return;
    float3 p = parameters.origin + (float3(i) + 0.5) * parameters.spacing;
    float3 offset = (p - parameters.center) / parameters.radius;
    float w = max(0.0, 1.0 - dot(offset, offset));
    w *= w;
    float dt = parameters.dt;
    float rho = min(1.0, density[i] * exp(-parameters.dissipation * dt) + dt * parameters.density_rate * w);
    float theta = temperature[i] * exp(-parameters.cooling * dt) + dt * parameters.heat_rate * w;
    float3 v = velocity[i].xyz;
    v.y += dt * parameters.buoyancy * theta;
    density[i] = rho;
    temperature[i] = theta;
    velocity[i] = float4(v, 0.0);
}
