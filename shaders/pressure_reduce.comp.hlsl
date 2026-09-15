#include "pressure_interface.hlsli"
groupshared float4 sums[64];
[numthreads(64, 1, 1)]
void main(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
    uint count = parameters.dimensions.x * parameters.dimensions.y * parameters.dimensions.z;
    float4 sum = 0;
    // 1024 cells per group; no float atomics or subgroup-size assumptions.
    for (uint i = group.x * 1024 + lane; i < min((group.x + 1) * 1024, count); i += 64) {
        uint3 p = uint3(i % parameters.dimensions.x,
            (i / parameters.dimensions.x) % parameters.dimensions.y,
            i / (parameters.dimensions.x * parameters.dimensions.y));
        float value;
        if (parameters.mode == 0) value = divergence.Load(int4(p, 0));
        else if (parameters.mode == 1) value = previous.Load(int4(p, 0));
        else if (parameters.mode == 3)
            value = (divergence.Load(int4(p, 0)) - statistics[0].x) * parameters.inverse_dt;
        else {
            float rhs = (divergence.Load(int4(p, 0)) - statistics[0].x) * parameters.inverse_dt;
            float laplacian = (neighbor_sum(p) - 6 * previous.Load(int4(p, 0))) / parameters.spacing_squared;
            value = rhs - laplacian;
        }
        float square = value * value;
        bool valid = isfinite(value) && isfinite(square);
        sum += valid ? float4(value, square, 0, 0) : float4(0, 0, 1, 0);
    }
    sums[lane] = sum;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32; stride; stride >>= 1) {
        if (lane < stride) sums[lane] += sums[lane + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0) partials[group.x] = sums[0];
}
