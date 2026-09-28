#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D ssaoTex;
layout(binding = 1) uniform sampler2D depthTex;
layout(binding = 2) uniform sampler2D sceneColorTex;

layout(push_constant) uniform BlurPushConstants {
    vec4 resolution; // x: width, y: height, z: 1/width, w: 1/height
    vec4 params;     // x: debugAO (1.0 or 0.0), y: intensity, z/w: unused
} pc;

void main() {
    vec2 texelSize = pc.resolution.zw;
    float centerDepth = texture(depthTex, inUV).r;

    // Sky or background: pass through unoccluded color
    if (centerDepth >= 0.999999) {
        if (pc.params.x > 0.5) {
            outColor = vec4(1.0, 1.0, 1.0, 1.0);
        } else {
            outColor = texture(sceneColorTex, inUV);
        }
        return;
    }

    // 4x4 Bilateral depth-aware blur
    float totalAO = 0.0;
    float totalWeight = 0.0;

    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec2 sampleUV = inUV + offset;

            float sampleDepth = texture(depthTex, sampleUV).r;
            float sampleAO = texture(ssaoTex, sampleUV).r;

            // Spatial Gaussian weight
            float spatialDist = float(x * x + y * y);
            float spatialWeight = exp(-spatialDist * 0.25);

            // Depth bilateral weight (preserves geometry silhouettes)
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * 500.0);

            float weight = spatialWeight * depthWeight;
            totalAO += sampleAO * weight;
            totalWeight += weight;
        }
    }

    float finalAO = (totalWeight > 0.0001) ? (totalAO / totalWeight) : texture(ssaoTex, inUV).r;
    finalAO = clamp(finalAO, 0.0, 1.0);

    // Debug AO mode: output pure black-and-white ambient occlusion factor
    if (pc.params.x > 0.5) {
        outColor = vec4(vec3(finalAO), 1.0);
        return;
    }

    // Standard mode: modulate scene ambient/color by SSAO
    vec4 sceneColor = texture(sceneColorTex, inUV);
    outColor = vec4(sceneColor.rgb * finalAO, sceneColor.a);
}
