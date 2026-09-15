#include "pressure_interface.hlsli"
groupshared float4 sums[64];
[numthreads(64, 1, 1)]
void main(uint lane : SV_GroupIndex) {
    float4 sum = 0;
    for (uint i = lane; i < parameters.groups; i += 64) sum += partials[i];
    sums[lane] = sum;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32; stride; stride >>= 1) {
        if (lane < stride) sums[lane] += sums[lane + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0) {
        float count = float(parameters.dimensions.x) * parameters.dimensions.y * parameters.dimensions.z;
        float4 moments = sums[0] / count;
        float4 result = statistics[0];
        if (parameters.mode == 0) {
            result.x = moments.x;

        } else if (parameters.mode == 1) result.z = moments.x;
        else if (parameters.mode == 3) result.y = sqrt(max(0.0, moments.y));
        else result.w = sqrt(max(0.0, moments.y));
        statistics[0] = result;
        statistics[1] += float4(sums[0].z, 0, 0, 0);
    }
}
