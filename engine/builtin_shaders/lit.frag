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

layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 1) uniform sampler2D normalSampler;
layout(set = 1, binding = 2) uniform sampler2D metallicSampler;

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

// Dynamic screenspace TBN calculation for normal mapping (NaN-safe fallback)
vec3 getNormalFromMap() {
    vec3 N = normalize(vNormal);
    vec3 tangentNormal = texture(normalSampler, vUV).xyz * 2.0 - 1.0;

    // Fast path: if the normal map is flat/default (0,0,1), avoid derivative artifacts
    if (length(tangentNormal.xy) < 0.03) {
        return N;
    }

    vec3 Q1  = dFdx(vWorldPos);
    vec3 Q2  = dFdy(vWorldPos);
    vec2 st1 = dFdx(vUV);
    vec2 st2 = dFdy(vUV);

    float det = st1.s * st2.t - st1.t * st2.s;
    vec3 T_dir = (Q1 * st2.t - Q2 * st1.t) * sign(det);
    vec3 T;
    if (length(T_dir) > 0.0001) {
        T = normalize(T_dir - N * dot(N, T_dir));
    } else {
        T = abs(N.y) < 0.999 ? normalize(cross(N, vec3(0.0, 1.0, 0.0))) : normalize(cross(N, vec3(1.0, 0.0, 0.0)));
    }

    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

// Samples directional shadow for a specific cascade using 16-tap Poisson disk PCF and receiver-plane bias
float sampleCascadeShadow(int cascadeIdx, vec3 geomN, vec3 L, float cosTheta) {
    // Dynamic slope-dependent bias
    float depthBias = max(cam.shadowParams.x * (1.0 - cosTheta), cam.shadowParams.x * 0.25);

    // Normal offset bias scaled for cascade
    float cascadeScale = (cascadeIdx == 0) ? 1.0 : (cam.cascadeSplits[cascadeIdx] / max(cam.cascadeSplits[0], 1.0));
    float tanSlope = sqrt(max(1.0 - cosTheta * cosTheta, 0.0)) / max(cosTheta, 0.05);
    float slopeFactor = clamp(tanSlope, 0.0, 3.0);
    vec3 shadowWorldPos = vWorldPos + geomN * (cam.shadowParams.y * cascadeScale * (1.0 + slopeFactor * 0.5));

    vec4 lightSpacePos = cam.cascadeLightSpaceMatrices[cascadeIdx] * vec4(shadowWorldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Fragments outside cascade light frustum cannot be sampled from this cascade
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return -1.0;
    }

    // Receiver-plane slope bias: calculate gradient of depth in light space
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

    // Per-pixel screen-space rotation for Poisson disk to break banding
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

// Cascaded Shadow Maps calculation with seamless split transitions, lateral fallback, and distance fade
float calculateDirectionalShadow(vec3 N, vec3 L) {
    if (cam.shadowParams.w < 0.5) {
        return 1.0; // Shadows disabled
    }

    vec3 geomN = normalize(vNormal);
    float cosTheta = dot(geomN, L);
    if (cosTheta <= 0.0) {
        return 0.0; // Surface backfaces the light source
    }

    vec4 clipPos = cam.viewProj * vec4(vWorldPos, 1.0);
    float viewDepth = (clipPos.w > 0.001) ? clipPos.w : length(vWorldPos - cam.camPos.xyz);

    // Beyond max shadow distance -> fully lit
    float maxDist = cam.cascadeSplits[3];
    if (viewDepth >= maxDist) {
        return 1.0;
    }

    int cascadeIdx = 0;
    for (int i = 0; i < 3; ++i) {
        if (viewDepth > cam.cascadeSplits[i]) {
            cascadeIdx = i + 1;
        }
    }

    // Try sampling from the depth-selected cascade. If the fragment lies outside the lateral
    // bounds of this cascade, gracefully fall through to larger enclosing cascades rather
    // than cutting the shadow off with an unnatural straight line!
    float shadow = 1.0;
    int actualCascade = -1;
    for (int c = cascadeIdx; c < 4; ++c) {
        float s = sampleCascadeShadow(c, geomN, L, cosTheta);
        if (s >= 0.0) {
            shadow = s;
            actualCascade = c;
            break;
        }
    }

    if (actualCascade < 0) {
        return 1.0; // Fragment is completely outside all shadow cascades
    }

    // Blend between cascades if near split boundary
    if (actualCascade < 3 && actualCascade == cascadeIdx) {
        float split = cam.cascadeSplits[actualCascade];
        float blendBand = split * 0.15;
        if (viewDepth > split - blendBand) {
            float blendFactor = clamp((viewDepth - (split - blendBand)) / blendBand, 0.0, 1.0);
            float nextShadow = sampleCascadeShadow(actualCascade + 1, geomN, L, cosTheta);
            if (nextShadow >= 0.0) {
                shadow = mix(shadow, nextShadow, blendFactor);
            }
        }
    }

    // Fade out shadow smoothly near maximum shadow distance
    float fadeStart = maxDist * 0.85;
    if (viewDepth > fadeStart) {
        float fade = clamp((maxDist - viewDepth) / (maxDist - fadeStart), 0.0, 1.0);
        shadow = mix(1.0, shadow, fade);
    }

    return shadow;
}

// Spot light shadow calculation using perspective depth projection and 4-tap PCF
float calculateSpotShadow(int spotIdx, int layerIdx, vec3 geomN, vec3 L, float bias, float normalBias) {
    float cosTheta = dot(geomN, L);
    if (cosTheta <= 0.0) {
        return 0.0;
    }

    vec3 shadowWorldPos = vWorldPos + geomN * (normalBias * 0.02);
    vec4 lightSpacePos = cam.spotLightSpaceMatrices[spotIdx] * vec4(shadowWorldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / max(0.00001, lightSpacePos.w);

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    float depthBias = max(bias * (1.0 - cosTheta), bias * 0.2);
    float texelSize = 1.0 / max(cam.shadowParams.z, 512.0);
    float filterRadius = texelSize * 1.5;

    float shadow = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 offset = POISSON_DISK[i] * filterRadius;
        shadow += texture(shadowMap, vec4(projCoords.xy + offset, float(layerIdx), projCoords.z - depthBias));
    }
    return shadow * 0.25;
}

// Omnidirectional point light shadow calculation using cubemap depth comparison with 4-tap PCF
float calculatePointShadow(vec3 lightPos, float range, vec3 geomN, vec3 L, float bias) {
    float cosTheta = dot(geomN, L);
    if (cosTheta <= 0.0) {
        return 0.0;
    }

    vec3 toFrag = vWorldPos - lightPos;
    float dist = length(toFrag);
    if (dist >= range) {
        return 1.0;
    }

    float maxComp = max(abs(toFrag.x), max(abs(toFrag.y), abs(toFrag.z)));
    float near = 0.1;
    float zNorm = (range / (range - near)) * (1.0 - near / max(maxComp, 0.0001));

    float depthBias = max(bias * (1.0 - cosTheta), bias * 0.25);
    float shadow = 0.0;
    const vec3 offsets[4] = vec3[](
        vec3( 0.01,  0.01,  0.01),
        vec3(-0.01,  0.01, -0.01),
        vec3( 0.01, -0.01, -0.01),
        vec3(-0.01, -0.01,  0.01)
    );
    for (int i = 0; i < 4; ++i) {
        shadow += texture(pointShadowMap, vec4(toFrag + offsets[i] * (dist * 0.02), zNorm - depthBias));
    }
    return shadow * 0.25;
}

// --- Procedural World-Space Puddle & Wetness Helpers ---
// Dave Hoskins trigonometric-free hash functions (stable across all GPU architectures)
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy) * 2.0 - 1.0;
}

vec4 hash42(vec2 p) {
    vec4 p4 = fract(vec4(p.xyxy) * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    p4 += dot(p4, p4.wzxy + 33.33);
    return fract((p4.xxyz + p4.yzzw) * p4.zywx);
}

// Quintic smooth interpolation for noise (zero 1st & 2nd derivatives at cell boundaries)
float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(dot(hash22(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
                   dot(hash22(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
               mix(dot(hash22(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                   dot(hash22(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

// Multi-octave FBM for natural puddle shapes with smooth shorelines
float puddleFBM(vec2 p) {
    float f = 0.0;
    f += 0.533 * noise2D(p * 0.12);
    f += 0.267 * noise2D(p * 0.28);
    f += 0.133 * noise2D(p * 0.60);
    f += 0.067 * noise2D(p * 1.30);
    return f * 0.5 + 0.5;
}

// Procedural multi-layer rain ripple generator:
// Seamless, stochastic raindrops strictly confined to standing water puddles
vec3 calculateRainRipples(vec2 worldPosXZ, float time, float intensity) {
    vec2 p = worldPosXZ * 2.0;
    vec2 totalNormal = vec2(0.0);

    const vec2 LAYER_OFFSETS[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(5.31, 7.89),
        vec2(12.77, 19.43)
    );
    const float LAYER_SPEEDS[3] = float[](1.35, 1.10, 1.60);

    for (int layer = 0; layer < 3; ++layer) {
        vec2 lp = p + LAYER_OFFSETS[layer];
        vec2 cell = floor(lp);
        vec2 f = fract(lp);

        vec4 h = hash42(cell + float(layer) * 37.19);

        // Stochastic raindrop arrival
        if (h.y > (intensity * 0.70 + 0.25)) {
            continue;
        }

        vec2 center = vec2(0.5) + (h.zw - 0.5) * 0.38;
        vec2 delta = f - center;
        float dist = length(delta);

        float dropTime = time * LAYER_SPEEDS[layer] + h.x;
        float progress = fract(dropTime);

        float waveRadius = progress * 0.36;
        float distDiff = dist - waveRadius;

        float waveEnvelope = exp(-distDiff * distDiff * 240.0);
        float lifeFade = (1.0 - progress) * (1.0 - progress) * smoothstep(0.0, 0.08, progress);
        float borderFade = smoothstep(0.46, 0.20, dist);

        float wave = sin(distDiff * 45.0) * waveEnvelope * lifeFade * borderFade;

        vec2 grad = (dist > 0.001) ? (delta / dist) : vec2(0.0);
        totalNormal += grad * wave;
    }

    return vec3(totalNormal.x, 0.0, totalNormal.y) * 0.14 * intensity;
}

void main() {
    vec4 baseColor = texture(texSampler, vUV) * vColor;
    if (baseColor.a < 0.01) {
        discard; // Early out for completely transparent pixels
    }

    vec3 N = getNormalFromMap();
    vec3 V = normalize(cam.camPos.xyz - vWorldPos);

    // Read roughness and metallic from texture and push constants
    vec4 metRough = texture(metallicSampler, vUV);
    float roughness = clamp(push.scale * metRough.g, 0.05, 0.95);
    float metallic  = clamp(push.fade * metRough.r, 0.0, 1.0);

    // --- Weather & Wetness Interaction ---
    float wetness = clamp(cam.weatherParams.x, 0.0, 1.0);
    float rainIntensity = clamp(cam.weatherParams.y, 0.0, 1.0);
    float puddleLevel = clamp(cam.weatherParams.z, 0.0, 1.0);
    float animTime = cam.weatherParams.w;

    float upDot = clamp(N.y, 0.0, 1.0);
    float wetFactor = wetness * mix(0.5, 1.0, upDot);

    // Procedural Puddles: accumulate in depressions on flat upward surfaces
    float puddleAmount = 0.0;
    if (puddleLevel > 0.01 && upDot > 0.45) {
        float puddleNoise = puddleFBM(vWorldPos.xz);
        float threshold = 0.85 - puddleLevel * 0.55;
        puddleAmount = smoothstep(threshold, threshold + 0.20, puddleNoise) * smoothstep(0.45, 0.80, upDot);
    }

    // Porous Albedo Darkening: dielectrics smoothly darken when drenched
    float porousDarkening = mix(1.0, 0.68, wetFactor * (1.0 - metallic));
    baseColor.rgb *= porousDarkening;

    // Roughness Reduction: wet surfaces become glossy; puddles become mirror-smooth
    roughness = mix(roughness, clamp(roughness * 0.30, 0.05, 0.22), wetFactor);
    roughness = mix(roughness, 0.02, puddleAmount);

    // Standing puddle water flattens normal towards pure up (0, 1, 0)
    // and raindrops perturb the puddle and wet surface
    if (puddleAmount > 0.01 || (rainIntensity > 0.01 && wetFactor > 0.15)) {
        vec3 puddleNormal = vec3(0.0, 1.0, 0.0);
        if (rainIntensity > 0.01) {
            vec3 ripplePerturb = calculateRainRipples(vWorldPos.xz, animTime, rainIntensity);
            puddleNormal = normalize(puddleNormal + ripplePerturb);
        }
        float blend = max(puddleAmount * 0.95, wetFactor * 0.35);
        N = normalize(mix(N, puddleNormal, blend));
    }

    // Specular shininess mapping (higher roughness = lower shininess)
    float shininess = exp2(10.0 * (1.0 - roughness) + 2.0);
    vec3 specularColor = mix(vec3(0.04), baseColor.rgb, metallic);

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
            // Point Light: vector pointing towards light source
            vec3 toLight = light.position.xyz - vWorldPos;
            float dist = length(toLight);
            float range = max(light.position.w, 0.001);
            if (dist >= range) {
                continue; // Pixel is beyond point light radius
            }
            L = (dist > 0.0001) ? (toLight / dist) : vec3(0.0, 1.0, 0.0);

            // Smooth range-based falloff
            float normDist = clamp(dist / range, 0.0, 1.0);
            float atten = 1.0 - normDist;
            attenuation = atten * atten;
        } else if (lightType == 2) {
            // Spot Light: vector pointing towards light source
            vec3 toLight = light.position.xyz - vWorldPos;
            float dist = length(toLight);
            float range = max(light.position.w, 0.001);
            if (dist >= range) {
                continue;
            }
            L = (dist > 0.0001) ? (toLight / dist) : vec3(0.0, 1.0, 0.0);

            float normDist = clamp(dist / range, 0.0, 1.0);
            float atten = 1.0 - normDist;
            attenuation = atten * atten;

            vec3 spotDir = normalize(light.direction.xyz);
            float cosAngle = dot(-L, spotDir);
            float innerCutoff = 0.9063; // cos(25 deg)
            float outerCutoff = 0.8191; // cos(35 deg)
            float spotFactor = clamp((cosAngle - outerCutoff) / (innerCutoff - outerCutoff), 0.0, 1.0);
            attenuation *= spotFactor;
            if (attenuation <= 0.0) {
                continue;
            }
        } else {
            // Directional Light: light.direction is light forward direction; vector towards light source is -light.direction
            L = normalize(-light.direction.xyz);
            attenuation = 1.0;
        }

        // Evaluate shadow per light source
        float shadow = 1.0;
        if (cam.shadowParams.w >= 0.5) {
            int shadowLayer = int(round(light.shadowInfo.x));
            float lightBias = (light.shadowInfo.y > 0.0) ? light.shadowInfo.y : cam.shadowParams.x;
            float lightNormBias = (light.shadowInfo.z > 0.0) ? light.shadowInfo.z : cam.shadowParams.y;

            if (lightType == 0) {
                // Directional light: use 4-cascade CSM if shadow enabled
                if (shadowLayer >= 0 || i == shadowLightIdx) {
                    shadow = calculateDirectionalShadow(N, L);
                }
            } else if (lightType == 2) {
                // Spot light: uses 2D shadow map array layers 4..7
                if (shadowLayer >= 4 && shadowLayer < 8) {
                    int spotIdx = shadowLayer - 4;
                    shadow = calculateSpotShadow(spotIdx, shadowLayer, N, L, lightBias, lightNormBias);
                }
            } else if (lightType == 1) {
                // Point light: uses cubemap depth comparison if hero point light
                int pointShadowIdx = int(round(cam.lightParams.z));
                if (i == pointShadowIdx && shadowLayer >= 0) {
                    shadow = calculatePointShadow(light.position.xyz, light.position.w, N, L, lightBias);
                }
            }
        }

        vec3 lightCol = light.color.rgb * light.color.a;
        vec3 H = normalize(L + V);

        // Diffuse lighting
        float diff = max(dot(N, L), 0.0);
        totalDiffuse += diff * baseColor.rgb * lightCol * (attenuation * shadow);

        // Specular highlight (Blinn-Phong)
        float spec = pow(max(dot(N, H), 0.0), shininess);
        totalSpecular += spec * specularColor * lightCol * (attenuation * shadow);
    }

    // Ambient fill comes from the renderer fallback sun (unaffected by shadow).
    vec3 ambient = (0.05 * cam.ambientLight.rgb + vec3(0.02)) * baseColor.rgb;

    // Sky / environment reflection for wet surfaces and puddles (Fresnel reflection)
    if (wetFactor > 0.01 || puddleAmount > 0.01) {
        vec3 R = reflect(-V, N);
        float NdotV = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
        vec3 skyRefl = cam.ambientLight.rgb * (1.0 + max(0.0, R.y) * 1.5);
        float reflFactor = mix(wetFactor * 0.45, 0.90, puddleAmount);
        float puddleSheen = puddleAmount * 0.08;
        totalSpecular += skyRefl * (fresnel * reflFactor + puddleSheen) * (1.0 - roughness * 0.5);
    }

    outColor = vec4(ambient + totalDiffuse + totalSpecular, baseColor.a);
}
