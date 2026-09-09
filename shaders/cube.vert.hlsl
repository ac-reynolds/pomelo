#include "cube_interface.hlsli"

VolumeVaryings main(uint vertex : SV_VertexID) {
    VolumeVaryings output;
    output.screen = float2((vertex << 1) & 2, vertex & 2) * 2.0 - 1.0;
    output.position = float4(output.screen, 0.0, 1.0);
    return output;
}
