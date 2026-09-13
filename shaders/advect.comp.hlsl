#include "volume_sampling.hlsli"
struct Step {
    uint3 dimensions;
    float dt_over_spacing;
};
[[vk::push_constant]] ConstantBuffer<Step> step;
[[vk::binding(0, 0)]] Texture3D<float> density;
[[vk::binding(1, 0)]] Texture3D<float4> velocity;
[[vk::binding(2, 0)]] RWTexture3D<float> density_destination;
[[vk::binding(3, 0)]] RWTexture3D<float4> velocity_destination;

[numthreads(4, 4, 4)]
void main(uint3 i : SV_DispatchThreadID) {
    if (any(i >= step.dimensions)) return;
    float3 departure = float3(i) - step.dt_over_spacing * velocity.Load(int4(i, 0)).xyz;
    // Both fields use the old velocity and the same departure point.
    density_destination[i] = periodic_trilinear(density, departure, step.dimensions);
    velocity_destination[i] = float4(periodic_trilinear(velocity, departure, step.dimensions).xyz, 0.0);
}
