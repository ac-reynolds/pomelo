struct Parameters {
    uint3 dimensions;
    float inverse_spacing;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[[vk::binding(0, 0)]] Texture3D<float4> velocity;
[[vk::binding(1, 0)]] RWTexture3D<float> divergence;

[numthreads(4, 4, 4)]
void main(uint3 i : SV_DispatchThreadID) {
    uint3 n = parameters.dimensions;
    if (any(i >= n)) return;
    uint3 lo = (i + n - 1) % n;
    // Backward divergence paired with the forward pressure gradient.
    float3 center = velocity.Load(int4(i, 0)).xyz;
    float dx = center.x - velocity.Load(int4(lo.x, i.y, i.z, 0)).x;
    float dy = center.y - velocity.Load(int4(i.x, lo.y, i.z, 0)).y;
    float dz = center.z - velocity.Load(int4(i.x, i.y, lo.z, 0)).z;
    divergence[i] = (dx + dy + dz) * parameters.inverse_spacing;
}
