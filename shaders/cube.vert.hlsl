#include "cube_interface.hlsli"

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 face_uv : TEXCOORD0;
};
struct Transform {
    column_major float4x4 mvp;
    float4 origin_spacing;
};
[[vk::push_constant]] ConstantBuffer<Transform> transform;
[[vk::binding(0, 0)]] Texture3D<float> grid_values;

CubeVaryings main(VertexInput input, uint instance : SV_InstanceID) {
    uint width, height, depth;
    grid_values.GetDimensions(width, height, depth);
    uint3 cell = uint3(instance % width, (instance / width) % height,
                       instance / (width * height));
    float3 center = transform.origin_spacing.xyz + (float3(cell) + 0.5) * transform.origin_spacing.w;
    float3 world = center + input.position * (0.5 * transform.origin_spacing.w);
    CubeVaryings output;
    output.position = mul(transform.mvp, float4(world, 1.0));
    output.cell_value = grid_values.Load(int4(cell, 0));
    output.uv = input.face_uv;
    return output;
}
