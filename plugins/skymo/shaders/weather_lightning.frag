#version 450

layout(location = 0) in vec2  inUV;
layout(location = 1) in float inIntensity;
layout(location = 2) in float inSegType;    // 0 = trunk, 1 = primary branch, 2 = secondary tendril

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inLightningTex;

void main() {
    // Distance from the ribbon center line [-1, 1] -> [0, 1]
    float d = abs(inUV.x - 0.5) * 2.0;

    // High-frequency electric arc crackle along the filament
    float crackle = sin(inUV.y * 180.0) * 0.035 + sin(inUV.y * 520.0) * 0.018;
    float dist = abs(d + crackle);

    // Tip fade: Trunk NEVER fades (solid 1.0, no ladder cuts!).
    // Branches/tendrils fade softly only at their very tips (last 25% of length).
    float tipFade = (inSegType < 0.5) ? 1.0 : smoothstep(1.0, 0.75, inUV.y);

    // Physical exponential plasma emission (authentic high-voltage discharge)
    // 1. Ultra-bright incandescent core filament (pure white)
    float core = exp(-dist * dist * 45.0);

    // 2. High-energy ionized gas envelope (clean electric cyan-white)
    float innerGlow = exp(-dist * 7.5);

    // 3. Diffuse atmospheric air ionization halo (soft electric sky-blue, zero purple)
    float outerHalo = exp(-dist * 2.4);

    vec3 coreColor  = vec3(1.00, 1.00, 1.00);
    vec3 innerColor = vec3(0.70, 0.88, 1.00);
    vec3 outerColor = vec3(0.45, 0.70, 1.00);

    // Branches carry proportionally less electrical current than the main discharge channel
    float branchFactor = (inSegType < 0.5) ? 1.0 : ((inSegType < 1.5) ? 0.65 : 0.40);
    float totalIntensity = inIntensity * branchFactor * tipFade;

    // Optional texture detail modulation if bound
    vec4 tex = texture(inLightningTex, inUV);
    float texDetail = mix(0.85, 1.15, tex.r);

    vec3 hdrColor =
        coreColor  * (core        * texDetail * totalIntensity * 85.0) +
        innerColor * (innerGlow               * totalIntensity * 18.0) +
        outerColor * (outerHalo               * totalIntensity *  4.5);

    // Alpha for additive depth blending
    float alpha = clamp(core + innerGlow * 0.55 + outerHalo * 0.25, 0.0, 1.0) * tipFade;
    if (alpha < 0.002) discard;

    outColor = vec4(hdrColor, alpha);
}
