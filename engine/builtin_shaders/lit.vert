#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj;
    vec4 camPos;
    float scale; // roughness
    float fade;  // metallic
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
    vec4 worldPos = push.model * vec4(inPos, 1.0);
    gl_Position = cam.viewProj * worldPos;
    
    vColor = push.color;
    vUV = inUV;
    
    // Transform normal to world space (handles rotation/scale part correctly under non-uniform scaling)
    vNormal = normalize(transpose(inverse(mat3(push.model))) * inNormal);
    vWorldPos = worldPos.xyz;
}
