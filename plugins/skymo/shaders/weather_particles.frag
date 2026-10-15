#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inWeatherType;
layout(location = 3) in float inPhase;      // Per-particle phase [0,1]

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inWeatherFxTex;

const float PI = 3.14159265358979;

// ─────────────────────────────────────────────────────────────────────────────
// SDF helpers
// ─────────────────────────────────────────────────────────────────────────────

// Signed distance to a capsule (rounded segment) from p to the segment a→b, radius r
float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Procedural 6-arm crystalline snowflake SDF
// p  : position in [-1, 1] space, center at (0,0)
// scale : arm reach (0.82 ≈ fills most of the [-1,1] square)
// Returns signed distance: < 0 inside the flake, > 0 outside
// ─────────────────────────────────────────────────────────────────────────────
float snowflakeSDF(vec2 p, float scale) {
    float r = length(p);

    // 6-fold symmetry: fold into one sextant [0, PI/3], then mirror at midpoint
    float angle = atan(p.y, p.x);
    angle = mod(angle + 2.0 * PI, 2.0 * PI);   // → [0, 2PI]
    angle = mod(angle, PI / 3.0);               // → [0, PI/3]
    if (angle > PI / 6.0) angle = PI / 3.0 - angle; // mirror → [0, PI/6]

    // Local coords in one sextant: arm points along +x
    vec2 q = r * vec2(cos(angle), sin(angle));

    float w   = 0.048 * scale;   // arm / branch half-thickness
    float len = scale;           // main arm length

    // ── 1. Main arm: capsule from center to tip ───────────────────────────
    float arm = sdCapsule(q, vec2(0.0), vec2(len, 0.0), w);

    // ── 2. Sub-branch A at 40 % along the arm ────────────────────────────
    //    Branches grow at ±30° from main arm; due to the mirror we only
    //    need to draw the upper branch (positive angle).
    float bAngle = PI / 6.0;                    // 30 degrees
    vec2  bDir   = vec2(cos(bAngle), sin(bAngle));
    float bA_ox  = 0.40 * len;                  // branch origin x
    float bA_len = 0.28 * len;                  // branch arm length
    float branch1 = sdCapsule(q, vec2(bA_ox, 0.0),
                               vec2(bA_ox, 0.0) + bDir * bA_len, w * 0.72);

    // ── 3. Sub-branch B at 68 % along the arm ────────────────────────────
    float bB_ox  = 0.68 * len;
    float bB_len = 0.18 * len;
    float branch2 = sdCapsule(q, vec2(bB_ox, 0.0),
                               vec2(bB_ox, 0.0) + bDir * bB_len, w * 0.60);

    // ── 4. Center hexagonal node (circle for simplicity) ─────────────────
    float centerNode = r - 0.11 * scale;

    return min(arm, min(branch1, min(branch2, centerNode)));
}

void main() {
    int weatherType = int(inWeatherType + 0.5);

    vec4 tex = texture(inWeatherFxTex, inUV);
    float particleAlpha = 0.0;

    if (weatherType == 2) {
        // ── Procedural crystalline snowflake with soft optical anti-aliasing ──
        vec2 p = (inUV - 0.5) * 2.2;

        float d = snowflakeSDF(p, 0.88);

        // Soft, anti-aliased edge transition (not razor-sharp, natural atmospheric look)
        float crystalShape = 1.0 - smoothstep(-0.06, 0.10, d);

        // Soft atmospheric halo around arms
        float armGlow = exp(-max(0.0, d) * 7.5) * 0.35;

        // Gentle central core body so flake feels like real translucent ice, not 1-pixel wireframe
        float softBody = exp(-length(p) * 3.8) * 0.45;

        particleAlpha = clamp(crystalShape * 0.75 + armGlow + softBody, 0.0, 1.0);
    } else {
        // ── Rain streak ───────────────────────────────────────────────────
        // Red channel holds the motion-blurred streak texture
        particleAlpha = tex.r * 0.92;

        // Smooth taper at the head and tail of the streak (UV.y axis)
        float lengthFade = smoothstep(0.0, 0.20, inUV.y) * smoothstep(1.0, 0.80, inUV.y);
        particleAlpha *= lengthFade;

        // Narrow bright core: the streak glints brighter at its centre axis
        float widthFade = 1.0 - smoothstep(0.35, 0.50, abs(inUV.x - 0.5) * 2.0);
        particleAlpha *= mix(0.55, 1.0, widthFade);
    }

    float finalAlpha = particleAlpha * inColor.a;
    if (finalAlpha < 0.008) discard;

    outColor = vec4(inColor.rgb * (weatherType == 2 ? 1.15 : 1.0), finalAlpha);
}
