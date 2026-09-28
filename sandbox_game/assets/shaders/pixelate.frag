#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D u_InputTexture;

layout(push_constant) uniform PostProcessPushConstants {
    vec4 u_Resolution; // x = width, y = height, z = 1/width, w = 1/height
    vec4 u_Params0;    // x = pixelSize (e.g. 4.0, 8.0)
    vec4 u_Params1;
    vec4 u_Params2;
    vec4 u_Params3;
} pc;

void main() {
    float pixelSize = max(1.0, pc.u_Params0.x);
    if (pixelSize <= 1.0) {
        outColor = texture(u_InputTexture, inUV);
        return;
    }

    vec2 res = pc.u_Resolution.xy;
    if (res.x <= 0.0 || res.y <= 0.0) {
        res = vec2(textureSize(u_InputTexture, 0));
    }

    // Offset by +0.5 to sample the exact center of each quantized pixel block
    vec2 coord = clamp((floor(inUV * res / pixelSize) + 0.5) * (pixelSize / res), vec2(0.0), vec2(1.0));
    outColor = texture(u_InputTexture, coord);
}
