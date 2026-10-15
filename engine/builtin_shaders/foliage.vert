#version 450

// Binding 0: Per-vertex geometry
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in ivec4 inBoneIDs;
layout(location = 4) in vec4 inBoneWeights;

// Binding 1: Per-instance data
layout(location = 5) in vec4 instancePosScale; // xyz = clump local pos, w = scale
layout(location = 6) in vec4 instanceRotWind;  // x = yaw, y = windPhase, z = tilt/sway, w = unused
layout(location = 7) in vec4 instanceColor;    // rgba tint

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj;
    vec4 camPos;
    float scale; // maxDistance (meters)
    float fade;  // windStrength
} push;

struct GPULight {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 shadowInfo;
};

const int MAX_LIGHTS = 16;
const int MAX_SHADOW_CASCADES = 4;
const int MAX_SPOT_SHADOWS = 4;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
    mat4 cascadeLightSpaceMatrices[MAX_SHADOW_CASCADES];
    mat4 spotLightSpaceMatrices[MAX_SPOT_SHADOWS];
    vec4 cascadeSplits;
    vec4 camPos;
    vec4 ambientLight;
    vec4 shadowParams;
    vec4 lightParams;
    vec4 weatherParams;
    GPULight lights[MAX_LIGHTS];
} cam;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vNormal;
layout(location = 3) out vec3 vWorldPos;

void main() {
    float yaw = instanceRotWind.x;
    float cosYaw = cos(yaw);
    float sinYaw = sin(yaw);

    // 1. Rotate vertex clump around local Y
    float clumpScale = max(instancePosScale.w, 0.05);
    vec3 localPos = inPos * clumpScale;
    vec3 rotatedPos = vec3(
        localPos.x * cosYaw - localPos.z * sinYaw,
        localPos.y,
        localPos.x * sinYaw + localPos.z * cosYaw
    );

    // 2. Base position in terrain local space
    vec3 terrainLocalPos = rotatedPos + instancePosScale.xyz;
    vec4 worldPos = push.model * vec4(terrainLocalPos, 1.0);

    // 3. Wind Animation: tips bend more than base (based on vertex Y position)
    float heightFraction = clamp(inPos.y, 0.0, 1.5);
    float windWeight = heightFraction * heightFraction; // Quadratic bend curve

    float time = cam.weatherParams.w * 2.2;
    float windPhase = instanceRotWind.y;
    // Spatial wave displacement across the terrain
    float wave = sin(time + worldPos.x * 0.2 + worldPos.z * 0.2 + windPhase);
    float gust = sin(time * 0.7 + worldPos.x * 0.07 + worldPos.z * 0.07);
    float windStrength = push.fade;

    vec3 windDir = normalize(vec3(0.85, 0.0, 0.52));
    vec3 windOffset = windDir * ((wave * 0.3 + gust * 0.15) * windStrength * (1.0 + instanceRotWind.z) * windWeight);
    // Slight downward dip as blade bends forward to preserve blade length
    windOffset.y -= length(windOffset.xz) * 0.18;

    worldPos.xyz += windOffset;

    // 4. Camera Distance Culling & Fade
    float dist = length(cam.camPos.xyz - worldPos.xyz);
    float maxDist = max(push.scale, 10.0);
    float fadeStart = maxDist * 0.75;
    float distFade = clamp((maxDist - dist) / (maxDist - fadeStart + 0.0001), 0.0, 1.0);

    // If completely outside distance, collapse vertex behind far clip
    if (distFade <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }

    gl_Position = cam.viewProj * worldPos;

    vUV = inUV;
    vWorldPos = worldPos.xyz;

    // 5. Rotated normal with slight upward bias for lush lighting
    vec3 rotatedNormal = vec3(
        inNormal.x * cosYaw - inNormal.z * sinYaw,
        inNormal.y,
        inNormal.x * sinYaw + inNormal.z * cosYaw
    );
    vec3 worldNormal = normalize(mat3(push.model) * rotatedNormal);
    vNormal = normalize(mix(worldNormal, vec3(0.0, 1.0, 0.0), 0.35));

    // Combine instance color tint, push constant tint, and distance fade
    vColor = instanceColor * push.color;
    vColor.a *= distFade;
}
