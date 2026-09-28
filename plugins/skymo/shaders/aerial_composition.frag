#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler3D inCameraVolumeLUT;
layout(set = 0, binding = 1) uniform sampler2D inDepthTexture;
layout(set = 0, binding = 2) uniform sampler2D inSkyViewLUT;
layout(set = 0, binding = 3) uniform sampler3D inCloudNoiseTex;
layout(set = 0, binding = 4) uniform CloudUBO {
    vec4 cloudAltitudes;  // x: bottomAltitude, y: topAltitude, z: coverage, w: density
    vec4 cloudModeling;   // x: cloudScale, y: detailScale, z: detailErosion, w: enabled (1.0 or 0.0)
    vec4 windParams;      // xy: windDirection, z: windSpeed, w: time
    vec4 lightingParams;  // x: silverLining, y: powderEffect, z: marchSteps, w: shadowIntensity
    vec4 cloudColor;      // xyz: cloudColor, w: lightningFlash
    vec4 ambientColor;    // xyz: ambientColor, w: unused
    vec4 weatherParams;   // x: weatherType, y: precipIntensity, z: wetness, w: lightningFlash
} clouds;

layout(push_constant) uniform AerialCompPushConstants {
    mat4 invViewProj;
    vec4 cameraPos;    // xyz: camPos, w: mode (0=AtmosphericHaze, 1=DenseFog)
    vec4 sunDir;       // xyz: sunDir, w: sunIntensity
    vec4 fogParams;    // x: nearDist, y: maxDist, z: densityMultiplier, w: affectSky (0.0 or 1.0)
    vec4 fogColorTint; // rgb: tint, w: unused
    vec4 sunScreen;    // xy: sunScreenUV, z: sunInFront (1.0 or 0.0), w: shaftIntensity
} pc;

const float PI = 3.14159265358979323846;
const float bottomR = 6360000.0;

void main() {
    float depth = texture(inDepthTexture, inUV).r;
    bool isSky = (depth >= 0.99999);

    float nearDist = max(0.1, pc.fogParams.x);
    float maxDist = max(nearDist + 5.0, pc.fogParams.y);
    float densityMul = max(0.0, pc.fogParams.z);
    bool affectSky = (pc.fogParams.w > 0.5);
    int mode = int(pc.cameraPos.w + 0.5);

    bool hasCloudShadows = (!isSky && clouds.cloudModeling.w > 0.5 && clouds.lightingParams.w > 0.001);
    bool hasWeather = (clouds.weatherParams.w > 0.001); // Lightning flash ambient tint

    if (densityMul <= 0.00001 && !hasCloudShadows && !hasWeather) {
        discard;
    }

    bool hasSunShafts = (pc.sunScreen.z > 0.5 && pc.sunScreen.w > 0.001);

    // Reconstruct world-space position and linear camera distance
    vec4 clip = vec4(inUV.x * 2.0 - 1.0, inUV.y * 2.0 - 1.0, depth, 1.0);
    vec4 worldH = pc.invViewProj * clip;
    vec3 worldPos = worldH.xyz / max(0.0001, worldH.w);
    float linearDist = length(worldPos - pc.cameraPos.xyz);

    // Map distance to exponential 3D LUT slice coordinate w in [0, 1]
    float w = 1.0;
    if (!isSky) {
        float clampedDist = clamp(linearDist, nearDist, maxDist);
        float logRatio = log(maxDist / nearDist);
        w = clamp(log(clampedDist / nearDist) / max(0.0001, logRatio), 0.0, 1.0);
    }

    // High-resolution screen-space dither to eliminate 8-bit color banding
    float dither = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;

    vec3 inscattering = vec3(0.0);
    float transmittance = 1.0;

    // Atmospheric volume fog contribution (geometry, or sky if affectSky is true)
    if (!isSky || affectSky) {
        if (isSky || linearDist > nearDist) {
            vec4 aerial = texture(inCameraVolumeLUT, vec3(inUV, w));
            inscattering = max(vec3(0.0), aerial.rgb + vec3(dither));
            transmittance = clamp(aerial.a, 0.0, 1.0);
        }
    }

    // Screen-space radial light shafts (crepuscular rays radiating from sun around silhouettes)
    if (hasSunShafts) {
        vec2 sunUV = pc.sunScreen.xy;
        vec2 deltaUV = (inUV - sunUV);

        // Aspect-corrected radial distance from sun disk (16:9 aspect ratio correction)
        vec2 aspectDelta = vec2(deltaUV.x * 1.7778, deltaUV.y);
        float distToSun = length(aspectDelta);

        // Raymarch from current pixel towards the sun in screen space
        const int NUM_STEPS = 40;
        vec2 stepUV = deltaUV * (1.0 / float(NUM_STEPS));
        // Sub-sample ray dithering breaks up discrete radial step bands into a silky smooth beam
        float rayDither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        vec2 curUV = inUV - stepUV * rayDither;

        float illuminationDecay = 1.0;
        const float decayFactor = 0.96;
        float shaftAccum = 0.0;
        float totalWeight = 0.0;

        for (int i = 0; i < NUM_STEPS; ++i) {
            float tapDepth = texture(inDepthTexture, curUV).r;
            // If tap is sky (depth >= 0.99999), direct sunlight streams through!
            // If tap is geometry (depth < 0.99999), the ray is occluded by the object!
            float unoccluded = (tapDepth >= 0.99999) ? 1.0 : 0.0;
            shaftAccum += unoccluded * illuminationDecay;
            totalWeight += illuminationDecay;
            illuminationDecay *= decayFactor;
            curUV -= stepUV;
        }

        float normAccum = shaftAccum / max(0.0001, totalWeight);
        // Contrast curve to produce crisp, dramatic rays radiating from silhouettes
        normAccum = pow(normAccum, 1.8);

        // Natural exponential solar corona falloff:
        // Highly concentrated around the solar disk, smoothly tapering into the surrounding sky.
        float radialFalloff = exp(-distToSun * 5.0) * clamp(1.0 - distToSun * 1.1, 0.0, 1.0);

        // Solar/Lunar shaft color modulated by user shadow intensity and fog density
        float densityFactor = clamp(densityMul * 1.5, 0.0, 1.0);
        vec3 lightShaftTint = (pc.sunDir.y >= 0.0) ? vec3(1.0, 0.95, 0.88) : vec3(0.65, 0.82, 1.0);
        vec3 shaftColor = lightShaftTint * (normAccum * radialFalloff * pc.sunScreen.w * densityFactor * 0.15);
        inscattering += shaftColor;
    }

    // Dynamic Volumetric Cloud Shadows projected onto scene geometry
    // 100% physically and geometrically locked to clouds in sky_composition.frag
    if (hasCloudShadows) {
        vec3 lightDir = normalize(pc.sunDir.xyz);
        if (lightDir.y > 0.02) {
            float bottomAlt = max(100.0, clouds.cloudAltitudes.x);
            float topAlt = max(bottomAlt + 200.0, clouds.cloudAltitudes.y);
            float coverage = clamp(clouds.cloudAltitudes.z, 0.0, 1.0);
            float densityMultiplier = clouds.cloudAltitudes.w;

            float cloudScale = clouds.cloudModeling.x;
            float detailScale = clouds.cloudModeling.y;
            float detailErosion = clouds.cloudModeling.z;
            float coverageThreshold = (1.0 - coverage) * 0.78;
            float shadowIntensity = clouds.lightingParams.w;

            vec2 windDir = clouds.windParams.xy;
            float windSpeed = clouds.windParams.z;
            float time = clouds.windParams.w;
            vec3 windOffset = vec3(windDir.x, 0.0, windDir.y) * (windSpeed * time);

            vec3 earthCenter = vec3(0.0, -bottomR, 0.0);

            // Distance along light ray from world position to bottom and top of cloud layer
            float tEnter = max(0.0, (bottomAlt - worldPos.y) / lightDir.y);
            float tExit = max(tEnter + 10.0, (topAlt - worldPos.y) / lightDir.y);
            float marchDist = tExit - tEnter;

            int numSteps = 8;
            float stepSize = marchDist / float(numSteps);
            float accumOpticalDepth = 0.0;

            for (int s = 0; s < numSteps; ++s) {
                float t = tEnter + stepSize * (float(s) + 0.5);
                vec3 pCloud = worldPos + lightDir * t;

                vec3 curEarth = pCloud - earthCenter;
                float curAlt = length(curEarth) - bottomR;
                float hNorm = clamp((curAlt - bottomAlt) / max(1.0, topAlt - bottomAlt), 0.0, 1.0);

                // Cumulus profile: flat crisp condensation base, organic towering cauliflower tops
                float heightGradient = smoothstep(0.0, 0.07, hNorm) * smoothstep(1.0, 0.75, hNorm);

                if (heightGradient > 0.001) {
                    vec3 samplePos = (pCloud + windOffset) * cloudScale;
                    float base = texture(inCloudNoiseTex, samplePos).r;
                    float baseDensity = smoothstep(coverageThreshold - 0.05, coverageThreshold + 0.12, base) * heightGradient;

                    if (baseDensity > 0.001) {
                        // High-frequency Worley detail sampled at detailScale to carve cauliflower tufts
                        vec3 detailPos = (pCloud + windOffset * 1.25) * detailScale;
                        vec4 detailNoise = texture(inCloudNoiseTex, detailPos);
                        float detail = dot(detailNoise.gba, vec3(0.50, 0.35, 0.15));
                        float edgeFactor = clamp(1.0 - baseDensity * 1.25, 0.0, 1.0);
                        float erodedDensity = clamp(baseDensity - detail * detailErosion * edgeFactor, 0.0, 1.0);
                        accumOpticalDepth += erodedDensity * stepSize * densityMultiplier * 0.012;
                    }
                }
            }

            // Physical Beer-Lambert transmittance through the cloud mass
            float shadowTransmittance = exp(-accumOpticalDepth * 2.5);

            // Modulate direct surface illumination
            float cloudShadow = mix(1.0 - shadowIntensity, 1.0, shadowTransmittance);
            transmittance *= cloudShadow;
        }
    }

    // Ambient Lightning Flash Tint on Scene Geometry
    float lightningFlash = clamp(clouds.weatherParams.w, 0.0, 1.0);
    if (lightningFlash > 0.001) {
        vec3 flashColor = vec3(0.92, 0.96, 1.04) * (lightningFlash * (isSky ? 0.35 : 0.95));
        inscattering += flashColor;
    }

    if (dot(inscattering, inscattering) < 1e-7 && transmittance > 0.9999) {
        discard;
    }

    // Final color blending:
    // OutColor = vec4(inscattering, transmittance)
    // Blend equation: FinalColor = inscattering * 1 + Framebuffer * transmittance
    outColor = vec4(inscattering, transmittance);
}
