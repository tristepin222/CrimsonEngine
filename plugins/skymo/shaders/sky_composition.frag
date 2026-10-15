#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inSkyViewLUT;
layout(set = 0, binding = 1) uniform sampler2D inMoonAlbedoTex;
layout(set = 0, binding = 2) uniform sampler3D inCloudNoiseTex;
layout(set = 0, binding = 3) uniform sampler2D inTransmittanceLUT;

layout(set = 0, binding = 4) uniform CloudUBO {
    vec4 cloudAltitudes;      // x: bottomAltitude, y: topAltitude, z: coverage, w: density
    vec4 cloudModeling;       // x: cloudScale, y: detailScale, z: detailErosion, w: enabled (1.0 or 0.0)
    vec4 windParams;          // xy: windDirection, z: windSpeed, w: time
    vec4 lightingParams;      // x: silverLining, y: powderEffect, z: marchSteps, w: shadowIntensity
    vec4 cloudColor;          // xyz: cloudColor, w: lightningFlash
    vec4 ambientColor;        // xyz: ambientColor, w: unused
    vec4 weatherParams;       // x: weatherType, y: precipIntensity, z: wetness, w: lightningFlash
} clouds;

layout(push_constant) uniform SkyCompositionPushConstants {
    mat4 invViewProj;
    vec4 cameraPos;   // xyz: camPos, w: exposure
    vec4 sunDir;      // xyz: sunDir, w: unused
    vec4 moonDir;     // xyz: moonDir, w: moonPhase (<0 auto, >=0 manual)
    vec4 nightParams; // x: starIntensity, y: moonIntensity, z: time, w: moonAngularRadius
} pc;

const float PI = 3.14159265358979323846;

// ----------------------------------------------------------------------------
// Procedural Hashing and Noise Utilities
// ----------------------------------------------------------------------------

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm3D(vec3 p) {
    float v = 0.0;
    v += 0.5000 * noise3D(p * 1.0);
    v += 0.2500 * noise3D(p * 2.02);
    v += 0.1250 * noise3D(p * 4.05);
    return v;
}

// ----------------------------------------------------------------------------
// Procedural Starfield
// ----------------------------------------------------------------------------

vec3 sampleStarLayer(vec3 rayDir, float freq, float threshold, float time, float seed) {
    vec3 p = rayDir * freq;
    vec3 cell = floor(p);
    vec3 f = fract(p);

    float cellRand = hash13(cell + seed);
    if (cellRand < threshold) return vec3(0.0);

    // Constrain star position to [0.2, 0.8] within cell to completely eliminate boundary clipping
    vec3 starPos = hash33(cell + seed * 1.618) * 0.6 + 0.2;
    float d = length(f - starPos);

    float starNorm = (cellRand - threshold) / (1.0 - threshold);
    float starRadius = mix(0.03, 0.08, starNorm);

    // Sharp anti-aliased core with soft outer glow
    float core = smoothstep(starRadius, 0.0, d);
    float glow = smoothstep(starRadius * 2.2, 0.0, d) * 0.25;
    float intensity = core + glow;

    // Twinkling effect
    float twinkle = sin(time * (1.5 + starNorm * 3.5) + cellRand * 62.83) * 0.35 + 0.65;

    // Spectral color distribution (cool blue-white, pure white, warm amber)
    float colorRand = hash13(cell + seed * 2.718);
    vec3 starColor = mix(vec3(1.0, 0.82, 0.6), vec3(0.72, 0.86, 1.0), colorRand);
    if (colorRand > 0.35 && colorRand < 0.7) starColor = vec3(1.0, 0.98, 0.95);

    return starColor * intensity * twinkle * (0.8 + starNorm * 1.8);
}

vec3 computeMilkyWay(vec3 rayDir) {
    vec3 galPole = normalize(vec3(0.45, 0.78, -0.43));
    float galDist = abs(dot(rayDir, galPole));
    float band = smoothstep(0.38, 0.0, galDist);

    float mwClouds = fbm3D(rayDir * 3.5);
    float dustRift = 1.0 - smoothstep(0.09, 0.0, galDist) * smoothstep(0.32, 0.68, noise3D(rayDir * 6.5 + vec3(1.2, 3.4, 5.6)));

    vec3 mwColor = mix(vec3(0.03, 0.035, 0.05), vec3(0.045, 0.04, 0.03), noise3D(rayDir * 2.0));
    return mwColor * band * mwClouds * dustRift * 1.6;
}

// ----------------------------------------------------------------------------
// Main Sky & Volumetric Cloud Composition
// ----------------------------------------------------------------------------

void main() {
    // Reconstruct world space ray from clip space
    vec4 clip = vec4(inUV * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldH = pc.invViewProj * clip;
    vec3 worldPos = worldH.xyz / worldH.w;
    vec3 rayDir = normalize(worldPos - pc.cameraPos.xyz);

    float bottomR = 6371000.0;
    float topR = 6471000.0;
    float r = bottomR + max(0.0, pc.cameraPos.y);

    float viewZenithCos = clamp(rayDir.y, -1.0, 1.0);
    vec3 sDir = normalize(pc.sunDir.xyz);

    // Azimuth angle relative to sun
    vec2 viewHoriz = vec2(rayDir.x, rayDir.z);
    vec2 sunHoriz = vec2(sDir.x, sDir.z);
    float u = 0.0;
    if (length(viewHoriz) > 0.0001 && length(sunHoriz) > 0.0001) {
        viewHoriz = normalize(viewHoriz);
        sunHoriz = normalize(sunHoriz);
        float cosPhi = clamp(dot(viewHoriz, sunHoriz), -1.0, 1.0);
        u = acos(cosPhi) / PI;
    }

    // Zenith angle uv coordinate with non-linear spacing
    float horizonCos = -sqrt(max(0.0, 1.0 - (bottomR * bottomR) / (r * r)));
    float v = 0.5;
    if (viewZenithCos < horizonCos) {
        float coord = max(0.0, (horizonCos - viewZenithCos) / max(0.0001, 1.0 + horizonCos));
        v = 0.5 * (1.0 - sqrt(coord));
    } else {
        float coord = max(0.0, (viewZenithCos - horizonCos) / max(0.0001, 1.0 - horizonCos));
        v = 0.5 + 0.5 * sqrt(coord);
    }
    v = clamp(v, 0.0, 1.0);

    vec3 skyColor = texture(inSkyViewLUT, vec2(u, v)).rgb;

    // Direct analytic sun disk (~0.5 degrees true astronomical angular diameter)
    float cosSunTheta = dot(rayDir, sDir);
    float sunRadius = 0.0048; // Physical sun angular radius
    float sunCosRadius = cos(sunRadius);
    if (viewZenithCos > horizonCos && cosSunTheta > sunCosRadius) {
        float edge = smoothstep(sunCosRadius, sunCosRadius + 0.0001, cosSunTheta);
        vec3 sunLuminance = vec3(1.0, 0.98, 0.92) * 15.0;
        skyColor += sunLuminance * edge;
    }

    // ------------------------------------------------------------------------
    // Analytic Moon Disk with NASA Photographic Texture & Tight Corona
    // ------------------------------------------------------------------------
    vec3 mDir = normalize(pc.moonDir.xyz);
    float moonRad = max(0.005, pc.nightParams.w);
    float cosMoonTheta = dot(rayDir, mDir);
    float moonCosRad = cos(moonRad);

    bool isMoonDisk = false;
    vec3 moonRadiance = vec3(0.0);
    float moonLimbAA = 0.0;

    // Atmospheric Lunar Corona / Halo (Tightly hugging the lunar limb)
    if (pc.nightParams.y > 0.001) {
        float moonAltitude = mDir.y + sin(moonRad);
        float moonHorizonFade = smoothstep(-0.02, 0.03, moonAltitude);

        float haloAngle = clamp(cosMoonTheta, 0.0, 1.0);
        float moonHalo = pow(haloAngle, 1200.0) * 0.35 + pow(haloAngle, 350.0) * 0.04;
        vec3 haloRadiance = vec3(0.85, 0.90, 1.0) * moonHalo * pc.nightParams.y * moonHorizonFade;
        skyColor += haloRadiance;

        // Tangent space projection on Moon disk
        if (cosMoonTheta > moonCosRad) {
            vec3 upRef = abs(mDir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
            vec3 moonT = normalize(cross(upRef, mDir));
            vec3 moonB = cross(mDir, moonT);

            float x = dot(rayDir, moonT) / sin(moonRad);
            float y = dot(rayDir, moonB) / sin(moonRad);
            float r2 = x * x + y * y;

            if (r2 <= 1.0) {
                isMoonDisk = true;
                moonLimbAA = smoothstep(1.0, 0.95, sqrt(r2)) * moonHorizonFade;

                // Sphere normal in tangent & world space
                float z = sqrt(max(0.0, 1.0 - r2));
                vec3 moonNormalWorld = normalize(x * moonT + y * moonB - z * mDir);

                // Sample NASA LROC Photographic Albedo Map via equirectangular sphere projection
                float moonU = clamp(atan(x, z) / (2.0 * PI) + 0.5, 0.0, 1.0);
                float moonV = clamp(asin(clamp(y, -1.0, 1.0)) / PI + 0.5, 0.0, 1.0);
                vec3 nasaAlbedo = texture(inMoonAlbedoTex, vec2(moonU, moonV)).rgb;

                // Calibrated lunar albedo with subtle crater micro-contrast
                vec3 moonAlbedo = pow(nasaAlbedo, vec3(1.15)) * 1.5;

                // Illumination & Phase
                float moonDiffuse = 0.0;
                if (pc.moonDir.w < 0.0) {
                    float NdotL = dot(moonNormalWorld, sDir);
                    moonDiffuse = max(0.0, NdotL) * smoothstep(-0.02, 0.06, NdotL);
                } else {
                    float phaseAngle = pc.moonDir.w * 2.0 * PI;
                    vec3 localLight = normalize(vec3(sin(phaseAngle), 0.0, -cos(phaseAngle)));
                    vec3 localNormal = vec3(x, y, z);
                    float manualNdotL = dot(localNormal, localLight);
                    moonDiffuse = max(0.0, manualNdotL) * smoothstep(-0.02, 0.06, manualNdotL);
                }

                // Earthshine (faint dark-side ambient glow from Earth)
                vec3 earthshine = vec3(0.015, 0.02, 0.03) * 0.2;

                moonRadiance = (moonAlbedo * moonDiffuse * 2.8 + earthshine * moonAlbedo) * pc.nightParams.y;
            }
        }
    }

    // ------------------------------------------------------------------------
    // Procedural Starfield & Milky Way (Strict Astronomical Daytime Extinction)
    // ------------------------------------------------------------------------
    if (pc.nightParams.x > 0.001 && !isMoonDisk) {
        float sunElevation = sDir.y;
        float dayExtinction = smoothstep(0.02, -0.15, sunElevation);

        float horizonFade = smoothstep(-0.01, 0.04, rayDir.y);
        float totalFade = dayExtinction * horizonFade * pc.nightParams.x;

        if (totalFade > 0.0001) {
            float time = pc.nightParams.z;
            vec3 stars = vec3(0.0);

            stars += sampleStarLayer(rayDir, 220.0, 0.75, time, 12.34);
            stars += sampleStarLayer(rayDir, 130.0, 0.88, time, 56.78);
            stars += sampleStarLayer(rayDir, 65.0, 0.96, time, 90.12);
            stars += computeMilkyWay(rayDir) * dayExtinction;

            skyColor += stars * totalFade;
        }
    }

    // Composite Moon over sky
    if (isMoonDisk && moonLimbAA > 0.001) {
        skyColor = mix(skyColor, moonRadiance, moonLimbAA);
    }

    // ------------------------------------------------------------------------
    // Volumetric Raymarched Clouds (3D Perlin-Worley with Sun & Moon Lighting)
    // ------------------------------------------------------------------------
    if (clouds.cloudModeling.w > 0.5 && clouds.cloudAltitudes.z > 0.001) {
        float bottomAlt = max(100.0, clouds.cloudAltitudes.x);
        float topAlt = max(bottomAlt + 200.0, clouds.cloudAltitudes.y);
        float coverage = clamp(clouds.cloudAltitudes.z, 0.0, 1.0);
        float densityMultiplier = clouds.cloudAltitudes.w;

        float R_bot = bottomR + bottomAlt;
        float R_top = bottomR + topAlt;

        // Camera in Earth-centered coordinates
        vec3 earthCenter = vec3(0.0, -bottomR, 0.0);
        vec3 camEarth = pc.cameraPos.xyz - earthCenter;

        float b = dot(camEarth, rayDir);
        float c_bot = dot(camEarth, camEarth) - R_bot * R_bot;
        float c_top = dot(camEarth, camEarth) - R_top * R_top;

        float d_bot = b * b - c_bot;
        float d_top = b * b - c_top;

        float tEnter = -1.0;
        float tExit = -1.0;

        if (d_top > 0.0) {
            float tTop1 = -b - sqrt(d_top);
            float tTop2 = -b + sqrt(d_top);

            if (d_bot > 0.0) {
                float tBot1 = -b - sqrt(d_bot);
                float tBot2 = -b + sqrt(d_bot);

                if (length(camEarth) < R_bot) {
                    if (tBot2 > 0.0) {
                        tEnter = tBot2;
                        tExit = tTop2;
                    }
                } else if (length(camEarth) > R_top) {
                    if (tTop1 > 0.0) {
                        tEnter = tTop1;
                        tExit = (tBot1 > 0.0) ? tBot1 : tTop2;
                    }
                } else {
                    tEnter = 0.0;
                    tExit = (tBot1 > 0.0) ? tBot1 : tTop2;
                }
            } else {
                tEnter = max(0.0, tTop1);
                tExit = tTop2;
            }
        }

        // Ground cutoff: only fade rays that aim into or below the ground terrain
        float groundFade = smoothstep(-0.005, 0.015, rayDir.y);

        // When cloud coverage is high (Overcast, Stormy), direct sunlight is blocked
        // Dim the background clear-sky horizon Rayleigh glow so the horizon looks authentically overcast
        float overcastFactor = smoothstep(0.45, 0.85, coverage);
        vec3 ambientTint = clouds.ambientColor.rgb;
        skyColor = mix(skyColor, ambientTint * 0.70, overcastFactor * 0.80);

        if (tEnter >= 0.0 && tExit > tEnter && groundFade > 0.001 && tEnter < 55000.0) {
            float marchDist = min(tExit - tEnter, 28000.0);

            int numSteps = int(clamp(clouds.lightingParams.z, 16.0, 96.0));
            float stepSize = marchDist / float(numSteps);

            // Full-step stochastic dither completely eliminates concentric slicing and scanline artifacts
            float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
            float curT = tEnter + stepSize * dither;

            float cloudScale = clouds.cloudModeling.x;
            float detailScale = clouds.cloudModeling.y;
            float detailErosion = clouds.cloudModeling.z;
            vec2 windDir = clouds.windParams.xy;
            float windSpeed = clouds.windParams.z;
            float time = clouds.windParams.w;
            vec3 windOffset = vec3(windDir.x, 0.0, windDir.y) * (windSpeed * time);

            float silverLining = clouds.lightingParams.x;
            float powderEffect = clouds.lightingParams.y;
            vec3 cloudAlbedo = clouds.cloudColor.rgb;

            bool isSunDominant = (sDir.y >= -0.02);
            vec3 dominantLightDir = isSunDominant ? sDir : normalize(-mDir);
            float dominantLightIntensity = isSunDominant ? 1.0 : (0.15 * pc.nightParams.y);
            vec3 dominantLightColor = isSunDominant ? vec3(1.0, 0.98, 0.92) : vec3(0.65, 0.82, 1.0);

            // Phase function: Dual-lobed Henyey-Greenstein
            float cosTheta = dot(rayDir, dominantLightDir);
            float g1 = 0.78;
            float hg1 = ((1.0 - g1 * g1) / pow(max(0.001, 1.0 + g1 * g1 - 2.0 * g1 * cosTheta), 1.5)) / (4.0 * PI);
            float g2 = -0.22;
            float hg2 = ((1.0 - g2 * g2) / pow(max(0.001, 1.0 + g2 * g2 - 2.0 * g2 * cosTheta), 1.5)) / (4.0 * PI);
            float cloudPhase = mix(hg2, hg1, 0.75) * silverLining;

            float cloudTransmittance = 1.0;
            vec3 cloudInscattering = vec3(0.0);

            float lightZenith = clamp(dominantLightDir.y, 0.0, 1.0);
            vec3 atmoLightTransmittance = texture(inTransmittanceLUT, vec2(0.5, lightZenith)).rgb;
            if (!isSunDominant) atmoLightTransmittance = vec3(0.7, 0.85, 1.0);

            // Physical coverage mapping: 0.0 = clear sky, 0.35-0.5 = fair/scattered cumulus, 1.0 = solid overcast
            float coverageThreshold = (1.0 - coverage) * 0.78;

            for (int i = 0; i < numSteps; ++i) {
                vec3 curPos = pc.cameraPos.xyz + rayDir * curT;
                vec3 curEarth = curPos - earthCenter;
                float curAlt = length(curEarth) - bottomR;
                float hNorm = clamp((curAlt - bottomAlt) / max(1.0, topAlt - bottomAlt), 0.0, 1.0);

                // Cumulus profile: flat crisp condensation base, organic towering cauliflower tops
                float heightGradient = smoothstep(0.0, 0.07, hNorm) * smoothstep(1.0, 0.75, hNorm);

                if (heightGradient > 0.001) {
                    vec3 samplePos = (curPos + windOffset) * cloudScale;
                    float base = texture(inCloudNoiseTex, samplePos).r;
                    // Defined density thresholding for crisp cauliflower lobes
                    float baseDensity = smoothstep(coverageThreshold - 0.05, coverageThreshold + 0.12, base) * heightGradient;

                    if (baseDensity > 0.001) {
                        // High-frequency Worley detail sampled at detailScale to carve cauliflower tufts
                        vec3 detailPos = (curPos + windOffset * 1.25) * detailScale;
                        vec4 detailNoise = texture(inCloudNoiseTex, detailPos);
                        float detail = dot(detailNoise.gba, vec3(0.50, 0.35, 0.15));
                        // Erode perimeters and edges into cauliflower billows without dissolving cloud cores
                        float edgeFactor = clamp(1.0 - baseDensity * 1.25, 0.0, 1.0);
                        float erodedDensity = clamp(baseDensity - detail * detailErosion * edgeFactor, 0.0, 1.0);
                        float stepDensity = erodedDensity * densityMultiplier;

                        if (stepDensity > 0.0001) {
                            // Light raymarch towards Sun / Moon (5 shadow samples of 130m)
                            float lightStepSize = 130.0;
                            float lightOpticalDepth = 0.0;

                            for (int j = 1; j <= 5; ++j) {
                                vec3 lPos = curPos + dominantLightDir * (float(j) * lightStepSize);
                                vec3 lEarth = lPos - earthCenter;
                                float lAlt = length(lEarth) - bottomR;
                                float lHNorm = clamp((lAlt - bottomAlt) / max(1.0, topAlt - bottomAlt), 0.0, 1.0);
                                float lHeightGrad = smoothstep(0.0, 0.07, lHNorm) * smoothstep(1.0, 0.75, lHNorm);

                                if (lHeightGrad > 0.001) {
                                    vec3 lSamplePos = (lPos + windOffset) * cloudScale;
                                    float lBase = texture(inCloudNoiseTex, lSamplePos).r;
                                    float lDens = smoothstep(coverageThreshold - 0.05, coverageThreshold + 0.12, lBase) * lHeightGrad;
                                    lightOpticalDepth += lDens * lightStepSize * densityMultiplier * 0.012;
                                }
                            }

                            // Physical Beer-Lambert attenuation with multiple-scattering
                            float lightTransmittance = exp(-lightOpticalDepth);
                            float ms = 1.0 + 0.85 * exp(-lightOpticalDepth * 0.22);
                            float stepOpticalDepth = stepDensity * stepSize * 0.012;

                            // Powder effect softens bright direct glare in deep folds
                            float powder = mix(1.0, 1.0 - exp(-stepOpticalDepth * 2.0), powderEffect * 0.6);

                            vec3 directLight = dominantLightColor * dominantLightIntensity * atmoLightTransmittance * (lightTransmittance * ms * cloudPhase * powder);
                            vec3 ambientLight = ambientTint * mix(0.45, 1.15, hNorm);

                            // Intra-cloud sheet lightning flash: illuminates thunderheads from inside
                            float lightningFlash = max(clouds.cloudColor.w, clouds.weatherParams.w);
                            if (lightningFlash > 0.001) {
                                vec3 lightningRadiance = vec3(0.90, 0.95, 1.05) * (lightningFlash * 35.0);
                                ambientLight += lightningRadiance * mix(1.2, 0.6, hNorm);
                            }

                            vec3 stepColor = (directLight + ambientLight) * cloudAlbedo;
                            float stepExtinction = exp(-stepOpticalDepth);

                            cloudInscattering += stepColor * (1.0 - stepExtinction) * cloudTransmittance;
                            cloudTransmittance *= stepExtinction;

                            if (cloudTransmittance < 0.01) {
                                cloudTransmittance = 0.0;
                                break;
                            }
                        }
                    }
                }
                curT += stepSize;
            }

            // Real-world aerial perspective: distant clouds near the horizon naturally dissolve into atmospheric haze
            vec3 cloudsComposited = skyColor * cloudTransmittance + cloudInscattering;
            float distHaze = 1.0 - exp(-tEnter * 0.000035);
            vec3 cloudsWithHaze = mix(cloudsComposited, skyColor, distHaze);

            skyColor = mix(skyColor, cloudsWithHaze, groundFade);
        }
    }

    // Output HDR sky color (Tonemapping/exposure applied in post-process)
    outColor = vec4(skyColor, 1.0);
}
