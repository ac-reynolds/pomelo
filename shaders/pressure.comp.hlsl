#include "pressure_interface.hlsli"
[numthreads(4, 4, 4)]
void main(uint3 p : SV_DispatchThreadID) {
    if (any(p >= parameters.dimensions)) return;
    float rhs = (divergence.Load(int4(p, 0)) - statistics[0].x) * parameters.inverse_dt;
    float candidate = (neighbor_sum(p) - parameters.spacing_squared * rhs) / 6;
    next[p] = lerp(previous.Load(int4(p, 0)), candidate, parameters.omega);
}
