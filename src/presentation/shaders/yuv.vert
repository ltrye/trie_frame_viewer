#version 440
layout(location = 0) in vec4 vertex;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 uv;
layout(std140, binding = 0) uniform buf {
    mat4 matrix;
    mat4 colorTransform;
    float opacity;
} uniforms;
void main() {
    uv = texCoord;
    gl_Position = uniforms.matrix * vertex;
}
