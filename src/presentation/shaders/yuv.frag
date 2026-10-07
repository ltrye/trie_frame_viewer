#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D planeY;
layout(binding = 2) uniform sampler2D planeU;
layout(binding = 3) uniform sampler2D planeV;
layout(std140, binding = 0) uniform buf {
    mat4 matrix;
    mat4 colorTransform;
    float opacity;
} uniforms;
void main() {
    vec4 yuv = vec4(texture(planeY, uv).r, texture(planeU, uv).r, texture(planeV, uv).r, 1.0);
    vec3 rgb = clamp((uniforms.colorTransform * yuv).rgb, 0.0, 1.0);
    fragColor = vec4(rgb, 1.0) * uniforms.opacity;
}
