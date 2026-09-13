#ifndef POMELO_VOLUME_SAMPLING_HLSLI
#define POMELO_VOLUME_SAMPLING_HLSLI
// Integer sample coordinates identify cell centers. Wrap neighbors independently.
float periodic_trilinear(Texture3D<float> values, float3 node, uint3 dimensions) {
    node = node - floor(node / float3(dimensions)) * float3(dimensions);
    int3 lo = int3(floor(node)) % int3(dimensions);
    int3 hi = (lo + 1) % int3(dimensions);
    float3 f = frac(node);
    float z0 = lerp(
        lerp(values.Load(int4(lo.x, lo.y, lo.z, 0)), values.Load(int4(hi.x, lo.y, lo.z, 0)), f.x),
        lerp(values.Load(int4(lo.x, hi.y, lo.z, 0)), values.Load(int4(hi.x, hi.y, lo.z, 0)), f.x), f.y);
    float z1 = lerp(
        lerp(values.Load(int4(lo.x, lo.y, hi.z, 0)), values.Load(int4(hi.x, lo.y, hi.z, 0)), f.x),
        lerp(values.Load(int4(lo.x, hi.y, hi.z, 0)), values.Load(int4(hi.x, hi.y, hi.z, 0)), f.x), f.y);
    return lerp(z0, z1, f.z);
}

#endif
