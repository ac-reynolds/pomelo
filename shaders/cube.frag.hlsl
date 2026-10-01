#include "volume_sampling.hlsli"
#include "cube_interface.hlsli"

struct Transform {
    float4 eye;
    float4 right;
    float4 up;
    float4 forward;
    float4 origin_spacing;
};
[[vk::push_constant]] ConstantBuffer<Transform> transform;
[[vk::binding(0, 0)]] Texture3D<float> grid_values;

// RGB emission color and extinction per grid-spacing unit.
float4 transfer(float value) {
    if (transform.eye.w > 0) {
        // Signed scalar: blue negative, orange positive; eye.w is inverse display scale.
        float strength = saturate(abs(value) * transform.eye.w);
        float3 color = value < 0 ? float3(0.1, 0.4, 1.0) : float3(1.0, 0.25, 0.05);
        return float4(color, 0.18 * strength);
    }
    float3 low = float3(0.025, 0.07, 0.25);
    float3 middle = float3(0.95, 0.30, 0.035);
    float3 high = float3(1.0, 0.92, 0.48);
    float3 color = value < 0.5 ? lerp(low, middle, value * 2.0)
                              : lerp(middle, high, (value - 0.5) * 2.0);
    return float4(color, 0.18 * smoothstep(0.25, 0.9, value));
}

// Antialiased grid on whichever of the six domain faces contains this point.
// Eight divisions per axis keep the box readable independently of resolution.
float face_grid(float3 position, float3 dimensions) {
    float3 face_distance = min(abs(position), abs(position - dimensions));
    uint normal = face_distance.x <= face_distance.y && face_distance.x <= face_distance.z
        ? 0 : (face_distance.y <= face_distance.z ? 1 : 2);
    float3 coordinates = position / dimensions * 8.0;
    float3 distance_to_line = abs(coordinates - round(coordinates));
    float3 pixel_width = max(fwidth(coordinates), 1e-5);
    float3 coverage = 1.0 - smoothstep(0.5, 1.5, distance_to_line / pixel_width);
    coverage[normal] = 0.0; // The face's constant coordinate is not a grid line.
    return max(coverage.x, max(coverage.y, coverage.z));
}

float4 main(VolumeVaryings input) : SV_Target0 {
    const float3 background = float3(0.055, 0.065, 0.085);
    uint width, height, depth;
    grid_values.GetDimensions(width, height, depth);
    uint3 dimensions = uint3(width, height, depth);
    float3 origin = (transform.eye.xyz - transform.origin_spacing.xyz) / transform.origin_spacing.w;
    // Positive viewport Y points down.
    float3 direction = normalize(transform.forward.xyz + input.screen.x * transform.right.xyz
                               - input.screen.y * transform.up.xyz);
    float entry = 0.0;
    float exit_distance = 1e30;
    [unroll]
    for (uint axis = 0; axis < 3; ++axis) {
        if (abs(direction[axis]) < 1e-8) {
            if (origin[axis] < 0.0 || origin[axis] > dimensions[axis])
                return float4(background, 1.0);
        } else {
            float a = -origin[axis] / direction[axis];
            float b = (float(dimensions[axis]) - origin[axis]) / direction[axis];
            entry = max(entry, min(a, b));
            exit_distance = min(exit_distance, max(a, b));
        }
    }
    if (exit_distance <= entry) return float4(background, 1.0);
    float front_grid = 0.0, back_grid = 0.0;
    if (transform.forward.w > 0.0) {
        front_grid = face_grid(origin + direction * entry, float3(dimensions));
        back_grid = face_grid(origin + direction * exit_distance, float3(dimensions));
    }
    const float3 grid_color = float3(0.45, 0.55, 0.65);
    float3 rear = lerp(background, grid_color, 0.3 * back_grid);
    float3 accumulated = 0.0;
    float transmittance = 1.0;
    // Uniform half-spacing intervals; integrate the final partial interval exactly.
    const float step_size = 0.5;
    [loop]
    for (float t = entry; t < exit_distance; t += step_size) {
        float segment = min(step_size, exit_distance - t);
        float value = periodic_trilinear(grid_values, origin + direction * (t + 0.5 * segment) - 0.5, dimensions);
        float4 material = transfer(value);
        float alpha = 1.0 - exp(-material.a * segment);
        accumulated += transmittance * alpha * material.rgb;
        transmittance *= 1.0 - alpha;
        if (transmittance < 0.001) break;
    }
    float3 color = accumulated + transmittance * rear;
    return float4(lerp(color, grid_color, 0.65 * front_grid), 1.0);
}
