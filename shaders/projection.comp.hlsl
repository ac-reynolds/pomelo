struct Parameters {
    uint3 dimensions;
    float dt_over_spacing;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[[vk::binding(0, 0)]] Texture3D<float> pressure;
[[vk::binding(1, 0)]] RWTexture3D<float4> velocity;

[numthreads(4, 4, 4)]
void main(uint3 i : SV_DispatchThreadID) {
    if (any(i >= parameters.dimensions)) return;
    uint3 hi = (i + 1) % parameters.dimensions;
    float p = pressure.Load(int4(i, 0));
    // Forward gradient pairs with backward divergence: D G is exactly the
    // nearest-neighbor seven-point Laplacian used by the pressure solve.
    float3 difference = float3(pressure.Load(int4(hi.x, i.y, i.z, 0)),
        pressure.Load(int4(i.x, hi.y, i.z, 0)), pressure.Load(int4(i.x, i.y, hi.z, 0))) - p;
    // No neighboring velocity reads, so in-place correction is race-free.
    velocity[i] = float4(velocity[i].xyz - parameters.dt_over_spacing * difference, 0);
}
