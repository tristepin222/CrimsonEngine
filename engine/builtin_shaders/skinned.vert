#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in ivec4 inBoneIDs;
layout(location = 4) in vec4 inBoneWeights;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj;
    vec4 camPos;
    float scale;
    float fade;
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

layout(set = 2, binding = 0) uniform JointPalette {
    mat4 joints[256]; // Max 256 bones supported (16 KB limit compliant)
} palette;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    float totalWeight = inBoneWeights.x + inBoneWeights.y + inBoneWeights.z + inBoneWeights.w;
    mat4 skinMat;
    
    if (totalWeight < 0.01) {
        skinMat = mat4(1.0); // Default to identity if no weights are defined
    } else {
        skinMat = 
            palette.joints[inBoneIDs.x] * inBoneWeights.x +
            palette.joints[inBoneIDs.y] * inBoneWeights.y +
            palette.joints[inBoneIDs.z] * inBoneWeights.z +
            palette.joints[inBoneIDs.w] * inBoneWeights.w;
        // Normalize weights just in case they don't sum to 1.0
        skinMat = skinMat / totalWeight;
    }

    vec4 skinnedPos = skinMat * vec4(inPos, 1.0);
    gl_Position = cam.viewProj * push.model * skinnedPos;
    
    vColor = push.color;
    vUV = inUV;
}
