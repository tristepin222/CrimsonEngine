#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj;
    vec4 camPos;
    float flipX;
    float flipY;
} push;

layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 1) uniform sampler2D normalSampler;   // Declared for layout compatibility
layout(set = 1, binding = 2) uniform sampler2D metallicSampler; // Declared for layout compatibility

layout(location = 0) out vec4 outColor;

void main() {
    vec4 baseColor = texture(texSampler, vUV) * vColor;
    if (baseColor.a < 0.01) {
        discard;
    }
    outColor = baseColor;
}
