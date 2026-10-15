#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inHDRTexture;

layout(push_constant) uniform TonemapPushConstants {
    int tonemapperMode; // 0 = ACES, 1 = Filmic, 2 = Reinhard, 3 = AgX, 4 = None
    float exposure;     // default 1.0
    float gamma;        // default 2.2
    float contrast;     // default 1.0
    float saturation;   // default 1.0
    float whitePoint;   // default 4.0
    float pad0;
    float pad1;
} pc;

// ACES Narkowicz 2015 approximation (industry standard)
vec3 tonemapACES(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Filmic / Unreal S-curve (Jim Hejl / Richard Cowgill curve)
vec3 tonemapFilmic(vec3 x) {
    vec3 X = max(vec3(0.0), x - 0.004);
    return (X * (6.2 * X + 0.5)) / (X * (6.2 * X + 1.7) + 0.06);
}

// Extended Reinhard with custom white point
vec3 tonemapReinhard(vec3 x, float white) {
    float w2 = max(0.1, white * white);
    float lOld = dot(x, vec3(0.2126, 0.7152, 0.0722));
    if (lOld <= 0.0001) return x;
    float lNew = (lOld * (1.0 + lOld / w2)) / (1.0 + lOld);
    return x * (lNew / lOld);
}

// AgX minimal approximation (Blender 4.0 style highlight handling)
vec3 tonemapAgX(vec3 x) {
    // Log-like curve with soft knee and desaturated highlights
    vec3 s = x / (x + vec3(0.18));
    return clamp(pow(s, vec3(1.15)), 0.0, 1.0);
}

void main() {
    vec4 hdrSample = texture(inHDRTexture, inUV);
    vec3 color = max(vec3(0.0), hdrSample.rgb);

    // 1. Exposure adjustment
    float expFactor = max(0.01, pc.exposure);
    color *= expFactor;

    // 2. Tone Mapping Operator
    vec3 ldr = color;
    if (pc.tonemapperMode == 0) {
        ldr = tonemapACES(color);
    } else if (pc.tonemapperMode == 1) {
        ldr = tonemapFilmic(color);
    } else if (pc.tonemapperMode == 2) {
        ldr = tonemapReinhard(color, max(1.0, pc.whitePoint));
    } else if (pc.tonemapperMode == 3) {
        ldr = tonemapAgX(color);
    } else {
        // Mode 4: Linear clamp
        ldr = clamp(color, 0.0, 1.0);
    }

    // 3. Contrast adjustment (pivoted at 0.5)
    if (abs(pc.contrast - 1.0) > 0.001) {
        ldr = clamp((ldr - 0.5) * pc.contrast + 0.5, 0.0, 1.0);
    }

    // 4. Saturation adjustment
    if (abs(pc.saturation - 1.0) > 0.001) {
        float luminance = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
        ldr = clamp(mix(vec3(luminance), ldr, pc.saturation), 0.0, 1.0);
    }

    // 5. Gamma Correction (Relative to sRGB 2.2 hardware curve handled by VK_FORMAT_B8G8R8A8_SRGB swapchain)
    float g = (pc.gamma > 0.1) ? pc.gamma : 2.2;
    if (abs(g - 2.2) > 0.01) {
        ldr = pow(ldr, vec3(2.2 / g));
    }

    outColor = vec4(ldr, hdrSample.a);
}
