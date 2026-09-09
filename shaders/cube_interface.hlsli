#ifndef POMELO_CUBE_INTERFACE_HLSLI
#define POMELO_CUBE_INTERFACE_HLSLI
struct VolumeVaryings {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 screen : TEXCOORD0;
};
#endif
