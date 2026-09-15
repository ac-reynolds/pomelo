struct Parameters {
    uint3 dimensions;
    float inverse_dt;
    float spacing_squared;
    float omega;
    uint groups;
    uint mode;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[[vk::binding(0, 0)]] Texture3D<float> divergence;
[[vk::binding(1, 0)]] Texture3D<float> previous;
[[vk::binding(2, 0)]] RWTexture3D<float> next;
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> partials;
// [0]: mean divergence, RMS RHS, mean pressure, RMS residual; [1].x: invalid count.
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> statistics;
float neighbor_sum(uint3 p) {
    uint3 lo = (p + parameters.dimensions - 1) % parameters.dimensions;
    uint3 hi = (p + 1) % parameters.dimensions;
    return previous.Load(int4(lo.x, p.y, p.z, 0)) + previous.Load(int4(hi.x, p.y, p.z, 0))
         + previous.Load(int4(p.x, lo.y, p.z, 0)) + previous.Load(int4(p.x, hi.y, p.z, 0))
         + previous.Load(int4(p.x, p.y, lo.z, 0)) + previous.Load(int4(p.x, p.y, hi.z, 0));
}
