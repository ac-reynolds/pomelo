#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 face_uv;
layout(location = 0) flat out float cell_value;
layout(location = 1) out vec2 uv;
layout(set = 0, binding = 0) uniform sampler3D grid_values;
layout(push_constant) uniform Transform { mat4 mvp; vec4 origin_spacing; } transform;
void main() {
    ivec3 dimensions = textureSize(grid_values, 0);
    int i = gl_InstanceIndex;
    ivec3 cell = ivec3(i % dimensions.x, (i / dimensions.x) % dimensions.y,
                      i / (dimensions.x * dimensions.y));
    vec3 center = transform.origin_spacing.xyz + (vec3(cell) + 0.5) * transform.origin_spacing.w;
    vec3 world = center + position * (0.5 * transform.origin_spacing.w);
    gl_Position = transform.mvp * vec4(world, 1.0);
    cell_value = texelFetch(grid_values, cell, 0).r;
    uv = face_uv;
}
