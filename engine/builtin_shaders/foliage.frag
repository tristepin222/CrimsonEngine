#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec3 vWorldPos;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj;
    vec4 camPos;
    float scale; // maxDistance
    float fade;  // windStrength
} push;

struct GPULight {
    vec4 position;  // xyz: pos, w: range
    vec4 direction; // xyz: dir, w: type (0 = Directional, 1 = Point, 2 = Spot)
    vec4 color;     // rgb: color, w: intensity
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

layout(set = 0, binding = 1) uniform sampler2DArrayShadow shadowMap;
layout(set = 0, binding = 2) uniform samplerCubeShadow pointShadowMap;

layout(set = 1, binding = 0) uniform sampler2D foliageTexture;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(foliageTexture, vUV);

    // Alpha test with smooth distance fade
    float alpha = texColor.a * vColor.a;
    if (alpha < 0.25) {
        discard;
    }

    vec3 albedo = texColor.rgb * vColor.rgb;

    // Two-sided normal
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) {
        N = -N;
    }

    // Directional sunlight extraction
    vec3 lightDir = normalize(vec3(0.3, 0.8, 0.4)); // Fallback sun direction
    vec3 lightCol = vec3(1.0, 0.98, 0.90);
    float lightIntensity = 1.0;

    int numLights = int(cam.lightParams.x);
    if (numLights > 0) {
        for (int i = 0; i < numLights; ++i) {
            if (cam.lights[i].direction.w == 0.0) { // Directional light
                lightDir = -normalize(cam.lights[i].direction.xyz);
                lightCol = cam.lights[i].color.rgb;
                lightIntensity = cam.lights[i].color.w;
                break;
            }
        }
    }

    // Wrapped diffuse lighting (Half-Lambert) gives lush, translucent foliage look
    float NdotL = dot(N, lightDir);
    float halfLambert = clamp((NdotL + 0.3) / 1.3, 0.0, 1.0);

    // Subsurface scattering: light coming through back of thin grass blade
    vec3 viewDir = normalize(cam.camPos.xyz - vWorldPos);
    float backLight = clamp(dot(-viewDir, lightDir), 0.0, 1.0);
    vec3 sss = albedo * lightCol * pow(backLight, 3.0) * 0.4;

    vec3 ambient = cam.ambientLight.rgb * 0.45;
    vec3 diffuse = albedo * lightCol * lightIntensity * halfLambert;

    vec3 finalRGB = ambient * albedo + diffuse + sss;

    outColor = vec4(finalRGB, 1.0);
}
