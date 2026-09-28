#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec2 vSplatUV;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj;
    vec4 camPos;
    float scale; // roughness multiplier
    float fade;  // metallic multiplier
} push;

struct GPULight {
    vec4 position;  // xyz: world position, w: range
    vec4 direction; // xyz: direction, w: type (0 = Directional, 1 = Point, 2 = Spot)
    vec4 color;     // rgb: color, w: intensity
    vec4 shadowInfo; // x: shadowLayerIndex (-1 if unshadowed), y: shadowBias, z: shadowNormalBias, w: unused
};

const int MAX_LIGHTS = 16;
const int MAX_SHADOW_CASCADES = 4;
const int MAX_SPOT_SHADOWS = 4;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
    mat4 cascadeLightSpaceMatrices[MAX_SHADOW_CASCADES]; // Sun CSM
    mat4 spotLightSpaceMatrices[MAX_SPOT_SHADOWS];       // Spot lights (layers 4..7)
    vec4 cascadeSplits; // View-space split depths: x, y, z, w
    vec4 camPos;        // camPos.xyz, w unused
    vec4 ambientLight;  // Renderer fallback sun used as ambient fill
    vec4 shadowParams;  // x: bias, y: normalBias, z: shadowMapRes, w: shadowEnabled (1.0 or 0.0)
    vec4 lightParams;   // x: numLights, y: primarySunIdx, z: pointShadowLightIdx, w: unused
    vec4 weatherParams; // x: wetness (0..1), y: rainIntensity (0..1), z: puddleLevel (0..1), w: time
    GPULight lights[MAX_LIGHTS];
} cam;

layout(set = 0, binding = 1) uniform sampler2DArrayShadow shadowMap;
layout(set = 0, binding = 2) uniform samplerCubeShadow pointShadowMap;

// Set 1: Terrain multi-texture splatmap bindings
layout(set = 1, binding = 0) uniform sampler2D splatMap;
layout(set = 1, binding = 1) uniform sampler2D layer0Albedo;
layout(set = 1, binding = 2) uniform sampler2D layer1Albedo;
layout(set = 1, binding = 3) uniform sampler2D layer2Albedo;
layout(set = 1, binding = 4) uniform sampler2D layer3Albedo;
layout(set = 1, binding = 5) uniform sampler2D layer0Normal;
layout(set = 1, binding = 6) uniform sampler2D layer1Normal;
layout(set = 1, binding = 7) uniform sampler2D layer2Normal;
layout(set = 1, binding = 8) uniform sampler2D layer3Normal;

layout(set = 1, binding = 9) uniform TerrainUBO {
    vec4 layerTiling;    // x: layer0, y: layer1, z: layer2, w: layer3
    vec4 layerRoughness; // x: layer0, y: layer1, z: layer2, w: layer3
    vec4 layerMetallic;  // x: layer0, y: layer1, z: layer2, w: layer3
    vec4 tintColor0;
    vec4 tintColor1;
    vec4 tintColor2;
    vec4 tintColor3;
    vec4 terrainParams;  // x: sizeX, y: sizeZ, z: heightScale, w: unused
} terrainUbo;

layout(location = 0) out vec4 outColor;

// 16 precomputed Poisson disk distribution samples on a unit disk
const vec2 POISSON_DISK[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

// Samples directional shadow for a specific cascade using 16-tap Poisson disk PCF
float sampleCascadeShadow(int cascadeIdx, vec3 geomN, vec3 L, float cosTheta) {
    float depthBias = max(cam.shadowParams.x * (1.0 - cosTheta), cam.shadowParams.x * 0.25);
    float cascadeScale = (cascadeIdx == 0) ? 1.0 : (cam.cascadeSplits[cascadeIdx] / max(cam.cascadeSplits[0], 1.0));
    float tanSlope = sqrt(max(1.0 - cosTheta * cosTheta, 0.0)) / max(cosTheta, 0.05);
    float slopeFactor = clamp(tanSlope, 0.0, 3.0);
    vec3 shadowWorldPos = vWorldPos + geomN * (cam.shadowParams.y * cascadeScale * (1.0 + slopeFactor * 0.5));

    vec4 lightSpacePos = cam.cascadeLightSpaceMatrices[cascadeIdx] * vec4(shadowWorldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return -1.0;
    }

    vec3 d1 = dFdx(projCoords);
    vec3 d2 = dFdy(projCoords);
    vec2 dDepth;
    float det = d1.x * d2.y - d1.y * d2.x;
    if (abs(det) > 1e-7) {
        dDepth.x = (d2.y * d1.z - d1.y * d2.z) / det;
        dDepth.y = (d1.x * d2.z - d2.x * d1.z) / det;
    } else {
        dDepth = vec2(0.0);
    }
    dDepth = clamp(dDepth, vec2(-2.5), vec2(2.5));

    float texelSize = 1.0 / max(cam.shadowParams.z, 512.0);
    float filterRadius = texelSize * 2.0;

    float angle = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float s = sin(angle);
    float c = cos(angle);
    mat2 rot = mat2(c, -s, s, c);

    float shadow = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = rot * POISSON_DISK[i] * filterRadius;
        float expectedPlaneDepth = projCoords.z + dot(dDepth, offset);
        shadow += texture(shadowMap, vec4(projCoords.xy + offset, float(cascadeIdx), expectedPlaneDepth - depthBias));
    }
    return shadow / 16.0;
}

float calculateDirectionalShadow(vec3 geomN, vec3 L) {
    float cosTheta = dot(geomN, L);
    if (cosTheta <= 0.0) {
        return 0.0;
    }

    vec4 viewPos = cam.viewProj * vec4(vWorldPos, 1.0);
    float depthValue = abs(viewPos.w);

    int cascadeIdx = 0;
    for (int i = 0; i < MAX_SHADOW_CASCADES - 1; ++i) {
        if (depthValue > cam.cascadeSplits[i]) {
            cascadeIdx = i + 1;
        }
    }

    float shadow = sampleCascadeShadow(cascadeIdx, geomN, L, cosTheta);
    if (shadow < 0.0) {
        for (int step = 1; step < MAX_SHADOW_CASCADES; ++step) {
            int alt = (cascadeIdx + step) % MAX_SHADOW_CASCADES;
            shadow = sampleCascadeShadow(alt, geomN, L, cosTheta);
            if (shadow >= 0.0) break;
        }
    }
    return clamp(shadow, 0.0, 1.0);
}

// Samples a 2D texture using triplanar projection weights to prevent vertical cliff stretching
vec4 sampleTriplanarLayer(sampler2D tex, vec3 worldPos, vec3 triW, float tiling) {
    if (triW.y > 0.95) {
        return texture(tex, worldPos.xz * tiling);
    }
    vec4 colY = texture(tex, worldPos.xz * tiling);
    vec4 colX = texture(tex, worldPos.zy * tiling);
    vec4 colZ = texture(tex, worldPos.xy * tiling);
    return colX * triW.x + colY * triW.y + colZ * triW.z;
}

vec3 sampleTriplanarNormal(sampler2D normMap, vec3 worldPos, vec3 triW, float tiling) {
    if (triW.y > 0.95) {
        return texture(normMap, worldPos.xz * tiling).xyz * 2.0 - 1.0;
    }
    vec3 nY = texture(normMap, worldPos.xz * tiling).xyz * 2.0 - 1.0;
    vec3 nX = texture(normMap, worldPos.zy * tiling).xyz * 2.0 - 1.0;
    vec3 nZ = texture(normMap, worldPos.xy * tiling).xyz * 2.0 - 1.0;
    return normalize(nX * triW.x + nY * triW.y + nZ * triW.z);
}

void main() {
    // 1. Sample splatmap weights
    vec4 splat = texture(splatMap, vSplatUV);
    float sumWeights = splat.r + splat.g + splat.b + splat.a;
    if (sumWeights > 0.0001) {
        splat /= sumWeights;
    } else {
        splat = vec4(1.0, 0.0, 0.0, 0.0);
    }

    // 2. Compute triplanar projection blending weights based on surface normal
    vec3 geomN = normalize(vNormal);
    vec3 triW = pow(abs(geomN), vec3(4.0));
    triW /= max(triW.x + triW.y + triW.z, 0.0001);

    // Continuous world-space UV tiling per layer
    vec2 uv0 = vWorldPos.xz * terrainUbo.layerTiling.x;
    vec2 uv1 = vWorldPos.xz * terrainUbo.layerTiling.y;
    vec2 uv2 = vWorldPos.xz * terrainUbo.layerTiling.z;
    vec2 uv3 = vWorldPos.xz * terrainUbo.layerTiling.w;

    bool isSteep = (triW.y <= 0.92);

    vec4 c0 = (isSteep && splat.r > 0.01)
        ? sampleTriplanarLayer(layer0Albedo, vWorldPos, triW, terrainUbo.layerTiling.x) * terrainUbo.tintColor0
        : texture(layer0Albedo, uv0) * terrainUbo.tintColor0;

    vec4 c1 = (isSteep && splat.g > 0.01)
        ? sampleTriplanarLayer(layer1Albedo, vWorldPos, triW, terrainUbo.layerTiling.y) * terrainUbo.tintColor1
        : texture(layer1Albedo, uv1) * terrainUbo.tintColor1;

    vec4 c2 = (isSteep && splat.b > 0.01)
        ? sampleTriplanarLayer(layer2Albedo, vWorldPos, triW, terrainUbo.layerTiling.z) * terrainUbo.tintColor2
        : texture(layer2Albedo, uv2) * terrainUbo.tintColor2;

    vec4 c3 = (isSteep && splat.a > 0.01)
        ? sampleTriplanarLayer(layer3Albedo, vWorldPos, triW, terrainUbo.layerTiling.w) * terrainUbo.tintColor3
        : texture(layer3Albedo, uv3) * terrainUbo.tintColor3;

    vec4 blendedColor = (c0 * splat.r + c1 * splat.g + c2 * splat.b + c3 * splat.a) * vColor;

    // 3. Normal mapping with slope-aware triplanar normals on steep cliff faces
    vec3 n0 = (isSteep && splat.r > 0.01)
        ? sampleTriplanarNormal(layer0Normal, vWorldPos, triW, terrainUbo.layerTiling.x)
        : texture(layer0Normal, uv0).xyz * 2.0 - 1.0;

    vec3 n1 = (isSteep && splat.g > 0.01)
        ? sampleTriplanarNormal(layer1Normal, vWorldPos, triW, terrainUbo.layerTiling.y)
        : texture(layer1Normal, uv1).xyz * 2.0 - 1.0;

    vec3 n2 = (isSteep && splat.b > 0.01)
        ? sampleTriplanarNormal(layer2Normal, vWorldPos, triW, terrainUbo.layerTiling.z)
        : texture(layer2Normal, uv2).xyz * 2.0 - 1.0;

    vec3 n3 = (isSteep && splat.a > 0.01)
        ? sampleTriplanarNormal(layer3Normal, vWorldPos, triW, terrainUbo.layerTiling.w)
        : texture(layer3Normal, uv3).xyz * 2.0 - 1.0;

    vec3 blendedTangentNormal = normalize(n0 * splat.r + n1 * splat.g + n2 * splat.b + n3 * splat.a);

    vec3 N = normalize(vNormal);
    if (length(blendedTangentNormal.xy) > 0.02) {
        vec3 T = abs(N.y) < 0.999 ? normalize(cross(vec3(0.0, 0.0, 1.0), N)) : vec3(1.0, 0.0, 0.0);
        vec3 B = normalize(cross(N, T));
        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * blendedTangentNormal);
    }

    vec3 V = normalize(cam.camPos.xyz - vWorldPos);

    // 4. Physical roughness & metallic blending
    float r0 = terrainUbo.layerRoughness.x;
    float r1 = terrainUbo.layerRoughness.y;
    float r2 = terrainUbo.layerRoughness.z;
    float r3 = terrainUbo.layerRoughness.w;
    float roughness = clamp(push.scale * (r0 * splat.r + r1 * splat.g + r2 * splat.b + r3 * splat.a), 0.05, 0.98);

    float m0 = terrainUbo.layerMetallic.x;
    float m1 = terrainUbo.layerMetallic.y;
    float m2 = terrainUbo.layerMetallic.z;
    float m3 = terrainUbo.layerMetallic.w;
    float metallic = clamp(push.fade * (m0 * splat.r + m1 * splat.g + m2 * splat.b + m3 * splat.a), 0.0, 1.0);

    float shininess = exp2(10.0 * (1.0 - roughness) + 2.0);
    vec3 specularColor = mix(vec3(0.04), blendedColor.rgb, metallic);

    vec3 totalDiffuse = vec3(0.0);
    vec3 totalSpecular = vec3(0.0);

    int numLights = clamp(int(round(cam.lightParams.x)), 0, MAX_LIGHTS);
    int shadowLightIdx = int(round(cam.lightParams.y));

    for (int i = 0; i < numLights; ++i) {
        GPULight light = cam.lights[i];
        int lightType = int(round(light.direction.w));
        vec3 L;
        float attenuation = 1.0;

        if (lightType == 1) {
            // Point Light
            vec3 toLight = light.position.xyz - vWorldPos;
            float dist = length(toLight);
            float range = max(light.position.w, 0.001);
            if (dist >= range) continue;
            L = (dist > 0.0001) ? (toLight / dist) : vec3(0.0, 1.0, 0.0);
            float normDist = clamp(dist / range, 0.0, 1.0);
            float atten = 1.0 - normDist;
            attenuation = atten * atten;
        } else if (lightType == 2) {
            // Spot Light
            vec3 toLight = light.position.xyz - vWorldPos;
            float dist = length(toLight);
            float range = max(light.position.w, 0.001);
            if (dist >= range) continue;
            L = (dist > 0.0001) ? (toLight / dist) : vec3(0.0, 1.0, 0.0);
            float normDist = clamp(dist / range, 0.0, 1.0);
            float atten = 1.0 - normDist;
            attenuation = atten * atten;

            vec3 spotDir = normalize(light.direction.xyz);
            float cosAngle = dot(-L, spotDir);
            float spotFactor = clamp((cosAngle - 0.8191) / (0.9063 - 0.8191), 0.0, 1.0);
            attenuation *= spotFactor;
            if (attenuation <= 0.0) continue;
        } else {
            // Directional Light
            L = normalize(-light.direction.xyz);
            attenuation = 1.0;
        }

        float shadow = 1.0;
        if (cam.shadowParams.w >= 0.5 && lightType == 0) {
            int shadowLayer = int(round(light.shadowInfo.x));
            if (shadowLayer >= 0 || i == shadowLightIdx) {
                shadow = calculateDirectionalShadow(N, L);
            }
        }

        vec3 lightCol = light.color.rgb * light.color.a;
        vec3 H = normalize(L + V);

        float diff = max(dot(N, L), 0.0);
        totalDiffuse += diff * blendedColor.rgb * lightCol * (attenuation * shadow);

        float spec = pow(max(dot(N, H), 0.0), shininess);
        totalSpecular += spec * specularColor * lightCol * (attenuation * shadow);
    }

    vec3 ambient = (0.08 * cam.ambientLight.rgb + vec3(0.03)) * blendedColor.rgb;
    outColor = vec4(ambient + totalDiffuse + totalSpecular, blendedColor.a);
}
