#include "cube_interface.hlsli"

float4 main(CubeVaryings input) : SV_Target0 {
    float3 low = float3(0.025, 0.07, 0.25);
    float3 middle = float3(0.95, 0.30, 0.035);
    float3 high = float3(1.0, 0.92, 0.48);
    float3 color = input.cell_value < 0.5 ? lerp(low, middle, input.cell_value * 2.0)
                                        : lerp(middle, high, (input.cell_value - 0.5) * 2.0);
    float2 edge_pixels = min(input.uv, 1.0 - input.uv) / max(fwidth(input.uv), float2(0.000001, 0.000001));
    float interior = smoothstep(0.3, 1.0, min(edge_pixels.x, edge_pixels.y));
    return float4(lerp(float3(0.008, 0.008, 0.008), color, interior), 1.0);
}
