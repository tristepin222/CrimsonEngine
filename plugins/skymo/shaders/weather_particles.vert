#version 450

layout(location = 0) in vec3 inSeedPos;      // Random seed in [0, 1]^3
layout(location = 1) in vec2 inCorner;       // Quad corner: x in [-0.5, 0.5], y in [-0.5, 0.5]
layout(location = 2) in vec2 inSizePhase;    // x: size, y: phase/speed variation [0, 1]

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;
layout(location = 2) out float outWeatherType;
layout(location = 3) out float outPhase;     // Per-particle phase [0,1] for gradient effects in frag

layout(push_constant) uniform PrecipPushConstants {
    mat4 viewProj;
    vec4 cameraPos;     // xyz: camPos, w: time
    vec4 windParams;    // xy: windDir, z: windSpeed, w: unused
    vec4 weatherParams; // x: weatherType (1=Rain, 2=Snow, 3=Storm), y: precipIntensity, z: unused, w: lightningFlash
    vec4 lightTint;     // rgb: ambient/sun tint, w: exposure
} pc;

void main() {
    float time = pc.cameraPos.w;
    int weatherType = int(pc.weatherParams.x + 0.5);
    float precipIntensity = pc.weatherParams.y;
    float lightningFlash = pc.weatherParams.w;

    outWeatherType = float(weatherType);
    outPhase = inSizePhase.y;

    // 3D Precipitation volume dimensions centered around active camera
    vec3 boxSize = (weatherType == 2) ? vec3(36.0, 22.0, 36.0) : vec3(32.0, 24.0, 32.0);
    vec3 halfBox = boxSize * 0.5;

    // Fall velocity (snow drifts gently ~2.2 m/s, rain plunges at ~22.0 m/s)
    vec3 fallVel = (weatherType == 2) ? vec3(0.0, -2.2, 0.0) : vec3(0.0, -22.0, 0.0);
    vec2 windDir = pc.windParams.xy;
    float windSpeed = pc.windParams.z;
    fallVel.xz += windDir * windSpeed * ((weatherType == 2) ? 0.85 : 0.35);

    // Per-particle speed variance
    fallVel *= (0.85 + inSizePhase.y * 0.3);

    // Base position within bounding box
    vec3 p0 = inSeedPos * boxSize;
    vec3 rawPos = p0 + fallVel * time;

    // Camera-relative volume wrapping: particles travel with the camera in 3D
    vec3 relPos = rawPos - pc.cameraPos.xyz;
    relPos = mod(relPos + halfBox, boxSize) - halfBox;

    // For snow: add natural sinusoidal horizontal turbulence & drift
    if (weatherType == 2) {
        float wobbleTime = time * 2.2 + inSizePhase.y * 6.28318;
        relPos.x += sin(wobbleTime) * 0.45;
        relPos.z += cos(wobbleTime * 0.85) * 0.45;
    } else {
        // For rain: add subtle wind-driven horizontal micro-turbulence per streak
        float turbPhase = inSizePhase.y * 6.28318;
        float turbAmp   = windSpeed * 0.012;
        relPos.x += sin(time * 4.5 + turbPhase) * turbAmp;
        relPos.z += cos(time * 3.8 + turbPhase * 1.3) * turbAmp;
    }

    vec3 centerPos = pc.cameraPos.xyz + relPos;

    // Vector pointing towards the camera for billboarding
    vec3 toCam = normalize(pc.cameraPos.xyz - centerPos);
    if (length(toCam) < 0.001) toCam = vec3(0.0, 0.0, 1.0);

    vec3 camRight = normalize(cross(vec3(0.0, 1.0, 0.0), toCam));
    if (length(camRight) < 0.001) camRight = vec3(1.0, 0.0, 0.0);
    vec3 camUp = cross(toCam, camRight);

    vec3 worldPos;
    if (weatherType == 2) {
        // Snow: Camera-facing square billboard with per-flake spin rotation
        // Size varies per-particle: smaller flakes (0.06) to bigger (0.20)
        float snowSize = (0.06 + inSizePhase.x * 0.14);

        // Per-flake rotation angle: static offset from phase + slow time spin
        float angle = inSizePhase.y * 6.28318 + time * (0.25 + inSizePhase.y * 0.35);
        float cosA = cos(angle), sinA = sin(angle);

        // Rotate corner around billboard center before projecting into world
        vec2 rotCorner = vec2(
            inCorner.x * cosA - inCorner.y * sinA,
            inCorner.x * sinA + inCorner.y * cosA
        );

        worldPos = centerPos + camRight * (rotCorner.x * snowSize) + camUp * (rotCorner.y * snowSize);

        // Rotated UV so texture also spins with the flake
        outUV = vec2(rotCorner.x + 0.5, rotCorner.y + 0.5);
    } else {
        // Rain: Velocity-aligned stretched ribbon billboard
        // Per-particle length variation: shorter thin streaks mix with longer ones
        float lengthMult = 0.45 + inSizePhase.y * 0.65;   // [0.45 … 1.10]
        float widthMult  = 0.70 + inSizePhase.x * 0.60;   // [0.70 … 1.30]

        vec3 velDir  = normalize(fallVel);
        vec3 rainRight = normalize(cross(velDir, toCam));
        if (length(rainRight) < 0.001) rainRight = camRight;

        float rainWidth  = inSizePhase.x * 0.028 * widthMult;
        float rainLength = inSizePhase.x * 0.65  * lengthMult;
        worldPos = centerPos + rainRight * (inCorner.x * rainWidth) + velDir * (inCorner.y * rainLength);

        // Standard UV (y used for gradient taper in fragment shader)
        outUV = inCorner + vec2(0.5);
    }

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);

    // Lighting calculation
    vec3 baseCol = (weatherType == 2) ? vec3(0.95, 0.98, 1.0) : vec3(0.80, 0.88, 1.0);
    vec3 litColor = baseCol * pc.lightTint.rgb;
    if (lightningFlash > 0.001) {
        litColor += vec3(0.90, 0.95, 1.35) * (lightningFlash * 3.5);
    }

    float alpha = precipIntensity;
    outColor = vec4(litColor, alpha);
}
