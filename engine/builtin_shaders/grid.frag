#version 450

layout(location = 0) in vec3 worldPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 color;
    vec4 camPos;
    float scale;
    float fade;
} push;

void main() {
    vec2 gridCoord = worldPos.xz / push.scale;
    vec2 f = abs(fract(gridCoord - 0.5) - 0.5);
    float line = min(f.x, f.y);

    // Screen-space derivative for crisp, anti-aliased grid lines
    vec2 d = fwidth(gridCoord);
    float lineThickness = max(0.015, min(d.x, d.y) * 1.5);
    float intensity = 1.0 - smoothstep(0.0, lineThickness, line);

    float fadeFactor = clamp(1.0 - length(worldPos.xz - push.camPos.xz) / push.fade, 0.0, 1.0);
    float alpha = intensity * fadeFactor * push.color.a;

    // Discard transparent interior cell fragments so zero depth is written to the Z-buffer
    if (alpha <= 0.001) {
        discard;
    }

    outColor = vec4(push.color.rgb, alpha);
}
