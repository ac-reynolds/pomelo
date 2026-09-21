#ifndef POMELO_VOLUME_SAMPLING_HLSLI
#define POMELO_VOLUME_SAMPLING_HLSLI
// Integer sample coordinates identify cell centers. Wrap neighbors independently.
// Keep the wrapped coordinate and fraction consistent near integer boundaries;
// reassociation can otherwise turn a tiny backtrace into a whole-cell shift.
float periodic_trilinear(Texture3D<float> values, precise float3 node, uint3 dimensions) {
    node = node - floor(node / float3(dimensions)) * float3(dimensions);
    int3 lo = int3(floor(node)) % int3(dimensions);
    int3 hi = (lo + 1) % int3(dimensions);
    precise float3 f = node - floor(node);
    float z0 = lerp(
        lerp(values.Load(int4(lo.x, lo.y, lo.z, 0)), values.Load(int4(hi.x, lo.y, lo.z, 0)), f.x),
        lerp(values.Load(int4(lo.x, hi.y, lo.z, 0)), values.Load(int4(hi.x, hi.y, lo.z, 0)), f.x), f.y);
    float z1 = lerp(
        lerp(values.Load(int4(lo.x, lo.y, hi.z, 0)), values.Load(int4(hi.x, lo.y, hi.z, 0)), f.x),
        lerp(values.Load(int4(lo.x, hi.y, hi.z, 0)), values.Load(int4(hi.x, hi.y, hi.z, 0)), f.x), f.y);
    return lerp(z0, z1, f.z);
}

float4 periodic_trilinear(Texture3D<float4> values, precise float3 node, uint3 dimensions) {
    node = node - floor(node / float3(dimensions)) * float3(dimensions);
    int3 lo = int3(floor(node)) % int3(dimensions);
    int3 hi = (lo + 1) % int3(dimensions);
    precise float3 f = node - floor(node);
    float4 z0 = lerp(
        lerp(values.Load(int4(lo.x, lo.y, lo.z, 0)), values.Load(int4(hi.x, lo.y, lo.z, 0)), f.x),
        lerp(values.Load(int4(lo.x, hi.y, lo.z, 0)), values.Load(int4(hi.x, hi.y, lo.z, 0)), f.x), f.y);
    float4 z1 = lerp(
        lerp(values.Load(int4(lo.x, lo.y, hi.z, 0)), values.Load(int4(hi.x, lo.y, hi.z, 0)), f.x),
        lerp(values.Load(int4(lo.x, hi.y, hi.z, 0)), values.Load(int4(hi.x, hi.y, hi.z, 0)), f.x), f.y);
    return lerp(z0, z1, f.z);
}

#endif
