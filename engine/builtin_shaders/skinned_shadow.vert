#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in ivec4 inBoneIDs;
layout(location = 4) in vec4 inBoneWeights;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj; // Holds lightSpaceMatrix during shadow pass
    vec4 camPos;
    float scale;
    float fade;
} push;

layout(set = 2, binding = 0) uniform JointPalette {
    mat4 joints[256];
} palette;

void main() {
    float totalWeight = inBoneWeights.x + inBoneWeights.y + inBoneWeights.z + inBoneWeights.w;
    mat4 skinMat;
    
    if (totalWeight < 0.01) {
        skinMat = mat4(1.0);
    } else {
        skinMat = 
            palette.joints[inBoneIDs.x] * inBoneWeights.x +
            palette.joints[inBoneIDs.y] * inBoneWeights.y +
            palette.joints[inBoneIDs.z] * inBoneWeights.z +
            palette.joints[inBoneIDs.w] * inBoneWeights.w;
        skinMat = skinMat / totalWeight;
    }

    vec4 skinnedPos = skinMat * vec4(inPos, 1.0);
    gl_Position = push.viewProj * push.model * skinnedPos;
}
