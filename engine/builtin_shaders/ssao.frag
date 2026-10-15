#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outAO;

layout(binding = 0) uniform sampler2D depthTex;

layout(binding = 1) uniform SSAOUBO {
    mat4 proj;
    mat4 invProj;
    vec4 params;      // x: radius, y: bias, z: intensity, w: power
    vec4 resolution;  // x: width, y: height, z: 1/width, w: 1/height
    ivec4 settings;   // x: sampleCount, y: debugAO, z/w: unused
    vec4 samples[32]; // hemisphere kernel samples
} ubo;

// Interleaved Gradient Noise (Jimenez 2014) - deterministic, zero texture lookups, blue-noise properties
float interleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

vec3 reconstructViewPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = ubo.invProj * clip;
    return view.xyz / view.w;
}

void main() {
    float depth = texture(depthTex, inUV).r;
    
    // Background / sky: no occlusion
    if (depth >= 0.999999) {
        outAO = 1.0;
        return;
    }

    vec3 fragPos = reconstructViewPos(inUV, depth);

    // Reconstruct view-space normal from depth screen derivatives
    vec3 dX = dFdx(fragPos);
    vec3 dY = dFdy(fragPos);
    vec3 normal = cross(dX, dY);
    if (dot(normal, -fragPos) < 0.0) {
        normal = -normal;
    }
    normal = normalize(normal);

    // Random rotation angle from screen pixel coordinate
    vec2 pixelCoord = inUV * ubo.resolution.xy;
    float noise = interleavedGradientNoise(pixelCoord);
    float angle = noise * 6.28318530718; // 2 * PI
    vec2 rot = vec2(cos(angle), sin(angle));

    // Construct an orthonormal tangent basis around normal
    vec3 tangent = abs(normal.z) < 0.999 ? cross(normal, vec3(0.0, 0.0, 1.0)) : cross(normal, vec3(1.0, 0.0, 0.0));
    tangent = normalize(tangent);
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float radius = ubo.params.x;
    float bias = ubo.params.y;
    int numSamples = clamp(ubo.settings.x, 4, 32);

    float occlusion = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        // Read precomputed hemisphere sample (oriented +Z)
        vec3 sampleVec = ubo.samples[i].xyz;

        // Rotate around normal in tangent plane
        vec3 rotatedSample = vec3(
            sampleVec.x * rot.x - sampleVec.y * rot.y,
            sampleVec.x * rot.y + sampleVec.y * rot.x,
            sampleVec.z
        );

        // Transform into view space
        vec3 sampleView = tbn * rotatedSample;
        vec3 samplePos = fragPos + sampleView * radius;

        // Project sample point to clip space and screen UV
        vec4 offset = ubo.proj * vec4(samplePos, 1.0);
        if (offset.w <= 0.0001) continue;
        vec2 sampleUV = (offset.xy / offset.w) * 0.5 + 0.5;

        // Skip samples falling outside the screen
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        // Sample scene depth at sampleUV
        float sampleSceneDepth = texture(depthTex, sampleUV).r;
        vec3 sampleScenePos = reconstructViewPos(sampleUV, sampleSceneDepth);

        // In right-handed view space, camera looks down -Z (Z is negative).
        // A point is closer to camera if its Z is greater than samplePos.Z.
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(0.001, abs(fragPos.z - sampleScenePos.z)));
        if (sampleScenePos.z >= samplePos.z + bias) {
            occlusion += rangeCheck;
        }
    }

    float ao = 1.0 - (occlusion / float(numSamples));
    float power = ubo.params.w;
    float intensity = ubo.params.z;
    ao = clamp(pow(clamp(ao, 0.0, 1.0), power) * intensity, 0.0, 1.0);

    outAO = ao;
}
