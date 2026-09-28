#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D u_InputTexture;

layout(push_constant) uniform PostProcessPushConstants {
    vec4 u_Resolution; // x = width, y = height, z = 1/width, w = 1/height
    vec4 u_Params0;    // x = blend factor (0.0 = original, 1.0 = full grayscale)
    vec4 u_Params1;
    vec4 u_Params2;
    vec4 u_Params3;
} pc;

void main() {
    vec4 col = texture(u_InputTexture, inUV);
    float factor = clamp(pc.u_Params0.x, 0.0, 1.0);
    float gray = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    outColor = vec4(mix(col.rgb, vec3(gray), factor), col.a);
}
