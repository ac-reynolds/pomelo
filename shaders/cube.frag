#version 450
layout(location = 0) flat in float cell_value;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 output_color;
void main() {
    vec3 low = vec3(0.025, 0.07, 0.25);
    vec3 middle = vec3(0.95, 0.30, 0.035);
    vec3 high = vec3(1.0, 0.92, 0.48);
    vec3 color = cell_value < 0.5 ? mix(low, middle, cell_value * 2.0)
                                : mix(middle, high, (cell_value - 0.5) * 2.0);
    vec2 edge_pixels = min(uv, 1.0 - uv) / max(fwidth(uv), vec2(0.000001));
    float interior = smoothstep(0.3, 1.0, min(edge_pixels.x, edge_pixels.y));
    output_color = vec4(mix(vec3(0.008), color, interior), 1.0);
}
