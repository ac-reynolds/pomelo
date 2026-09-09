#ifndef POMELO_CUBE_INTERFACE_HLSLI
#define POMELO_CUBE_INTERFACE_HLSLI
struct CubeVaryings {
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation float cell_value : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
};
#endif
