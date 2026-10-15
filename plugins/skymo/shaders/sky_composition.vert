#version 450

layout(location = 0) out vec2 outUV;

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    // Vulkan clip space depth is [0, 1]. Render at far plane z = 1.0.
    gl_Position = vec4(outUV * 2.0f - 1.0f, 1.0f, 1.0f);
}
