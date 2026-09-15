#include "pressure_interface.hlsli"
[numthreads(4, 4, 4)]
void main(uint3 p : SV_DispatchThreadID) {
    if (all(p < parameters.dimensions)) next[p] -= statistics[0].z;
}
