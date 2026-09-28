#version 450

layout(location = 0) out vec2  outUV;
layout(location = 1) out float outIntensity;
layout(location = 2) out float outSegType;   // 0 = trunk, 1 = primary branch, 2 = secondary tendril

layout(push_constant) uniform LightningPushConstants {
    mat4 viewProj;
    vec4 strikePos;  // xyz: ground strike point (or CC crawler end), w: trunk half-width (world units)
    vec4 cloudPos;   // xyz: cloud base strike point, w: flashIntensity
    vec4 cameraPos;  // xyz: cameraPos, w: packed strikeType * 10.0 + seed
} pc;

// ─────────────────────────────────────────────────────────────────────────────
// Deterministic hash function
// ─────────────────────────────────────────────────────────────────────────────
float hash(float a, float b) {
    return fract(sin(dot(vec2(a, b), vec2(127.1, 311.7))) * 43758.5453);
}

// ─────────────────────────────────────────────────────────────────────────────
// Total Segments: 64 Quads (384 vertices)
// Segments  0 .. 35 : Main continuous stepped-leader trunk (36 segments, 37 joints)
// Segments 36 .. 51 : 4 primary branches (4 segments each = 16 segments)
// Segments 52 .. 63 : 4 secondary tendrils / cloud crawlers (3 segments each = 12 segments)
// ─────────────────────────────────────────────────────────────────────────────
const int NUM_SEGMENTS = 64;

const vec2 QUAD_CORNERS[6] = vec2[](
    vec2(0.0, 0.0), // bottom-left
    vec2(1.0, 0.0), // bottom-right
    vec2(1.0, 1.0), // top-right
    vec2(1.0, 1.0), // top-right
    vec2(0.0, 1.0), // top-left
    vec2(0.0, 0.0)  // bottom-left
);

// ─────────────────────────────────────────────────────────────────────────────
// Continuous stepped-leader trunk joint evaluator (j in 0 .. 36)
// ─────────────────────────────────────────────────────────────────────────────
vec3 evalTrunkJointPos(int j, vec3 gnd, vec3 cld, vec3 globalRight, vec3 globalFwd, float seed, float maxJag) {
    if (j <= 0) return gnd;
    if (j >= 36) return cld;

    float t = float(j) / 36.0;
    vec3 baseP = mix(gnd, cld, t);
    float env = sin(t * 3.14159265);

    // Stepped leader: every 3 joints forms a discrete stepped kink
    float stepIdx = floor(float(j) / 3.0);

    // Macro wander: large-scale trajectory deflection
    float wR1 = hash(seed + 1.1, floor(t * 5.0)) - 0.5;
    float wR2 = hash(seed + 1.1, floor(t * 5.0) + 1.0) - 0.5;
    float macroR = mix(wR1, wR2, smoothstep(0.0, 1.0, fract(t * 5.0))) * maxJag * 1.15;

    float wF1 = hash(seed + 7.3, floor(t * 4.0)) - 0.5;
    float wF2 = hash(seed + 7.3, floor(t * 4.0) + 1.0) - 0.5;
    float macroF = mix(wF1, wF2, smoothstep(0.0, 1.0, fract(t * 4.0))) * maxJag * 0.85;

    // Stepped leader kink (sharp angular directional change at each step)
    float kinkR = (hash(seed + 13.7, stepIdx) - 0.5) * maxJag * 0.50;
    float kinkF = (hash(seed + 29.1, stepIdx) - 0.5) * maxJag * 0.35;

    // Electric micro-jitter at individual joints
    float microR = (hash(seed + 41.3, float(j)) - 0.5) * maxJag * 0.16;
    float microF = (hash(seed + 59.7, float(j)) - 0.5) * maxJag * 0.12;

    float totalR = (macroR + kinkR + microR) * env;
    float totalF = (macroF + kinkF + microF) * env;

    return baseP + globalRight * totalR + globalFwd * totalF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Branch joint evaluator (jb in 0 .. 4)
// ─────────────────────────────────────────────────────────────────────────────
vec3 evalBranchJointPos(int jb, vec3 forkOrigin, vec3 branchDir, vec3 globalRight, vec3 globalFwd, float bSeed, float branchLen, float maxJag) {
    if (jb <= 0) return forkOrigin;
    float frac = float(jb) / 4.0;
    vec3 stepP = forkOrigin + branchDir * (branchLen * frac);

    // Jagged stepped offsets at each branch joint
    float jagR = (hash(bSeed + 7.1, float(jb)) - 0.5) * (maxJag * 0.35);
    float jagF = (hash(bSeed + 9.3, float(jb)) - 0.5) * (maxJag * 0.25);
    return stepP + globalRight * jagR + globalFwd * jagF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tendril joint evaluator (jt in 0 .. 3)
// ─────────────────────────────────────────────────────────────────────────────
vec3 evalTendrilJointPos(int jt, vec3 tOrigin, vec3 tDir, vec3 globalRight, vec3 globalFwd, float tSeed, float tLen, float maxJag) {
    if (jt <= 0) return tOrigin;
    float frac = float(jt) / 3.0;
    vec3 stepP = tOrigin + tDir * (tLen * frac);

    float jagR = (hash(tSeed + 13.1, float(jt)) - 0.5) * (maxJag * 0.22);
    float jagF = (hash(tSeed + 17.3, float(jt)) - 0.5) * (maxJag * 0.18);
    return stepP + globalRight * jagR + globalFwd * jagF;
}

void main() {
    int segIdx    = gl_VertexIndex / 6;
    int cornerIdx = gl_VertexIndex % 6;

    vec2 cornerUV = QUAD_CORNERS[cornerIdx];
    outIntensity  = pc.cloudPos.w;   // flashIntensity

    int strikeType = int(floor(pc.cameraPos.w / 10.0 + 0.05));
    float seed     = fract(pc.cameraPos.w);
    if (seed < 0.001) seed = 0.4183;

    // Realistic physical thin arc half-width (clamped, never a 50m pillar)
    float trunkHalf = clamp(pc.strikePos.w, 0.40, 4.5);

    vec3 gnd = pc.strikePos.xyz;
    vec3 cld = pc.cloudPos.xyz;

    vec3 boltDir = normalize(cld - gnd);
    if (length(boltDir) < 0.001) boltDir = vec3(0.0, 1.0, 0.0);

    vec3 toCamGlobal = normalize(pc.cameraPos.xyz - mix(gnd, cld, 0.5));
    if (length(toCamGlobal) < 0.001) toCamGlobal = vec3(0.0, 0.0, 1.0);

    vec3 globalRight = normalize(cross(boltDir, toCamGlobal));
    if (length(globalRight) < 0.001) globalRight = vec3(1.0, 0.0, 0.0);

    vec3 globalFwd = normalize(cross(globalRight, boltDir));
    if (length(globalFwd) < 0.001) globalFwd = vec3(0.0, 0.0, 1.0);

    float spanLen = length(cld - gnd);
    float maxJag = clamp(spanLen * 0.075, 20.0, 380.0);

    vec3 worldPos = vec3(0.0);

    // ─────────────────────────────────────────────────────────────────────────
    // 1. Main Trunk (Segments 0 .. 35: 36 continuous, seamlessly shared quads)
    // ─────────────────────────────────────────────────────────────────────────
    if (segIdx < 36) {
        outSegType = 0.0;

        // Joint index at this vertex: cornerUV.y == 0 -> joint segIdx, cornerUV.y == 1 -> joint segIdx + 1
        int jointIdx = (cornerUV.y > 0.5) ? (segIdx + 1) : segIdx;

        // Continuous UV.y along the entire trunk from ground (0.0) to cloud base (1.0)
        outUV = vec2(cornerUV.x, (float(segIdx) + cornerUV.y) / 36.0);

        // Deterministic joint position shared identically with adjacent segment
        vec3 pJoint = evalTrunkJointPos(jointIdx, gnd, cld, globalRight, globalFwd, seed, maxJag);

        // Local joint tangent for smooth ribbon billboarding
        int prevJ = max(0, jointIdx - 1);
        int nextJ = min(36, jointIdx + 1);
        vec3 pPrev = evalTrunkJointPos(prevJ, gnd, cld, globalRight, globalFwd, seed, maxJag);
        vec3 pNext = evalTrunkJointPos(nextJ, gnd, cld, globalRight, globalFwd, seed, maxJag);
        vec3 jTangent = normalize(pNext - pPrev);

        vec3 toCam = normalize(pc.cameraPos.xyz - pJoint);
        vec3 jRight = normalize(cross(jTangent, toCam));
        if (length(jRight) < 0.001) jRight = globalRight;

        // Trunk thickness tapers naturally from top (cloud) to bottom
        float tJoint = float(jointIdx) / 36.0;
        float hw = trunkHalf * mix(0.75, 1.0, tJoint);

        worldPos = pJoint + jRight * ((cornerUV.x - 0.5) * hw * 2.0);
    }
    // ─────────────────────────────────────────────────────────────────────────
    // 2. Primary Branches (Segments 36 .. 51: 4 branches × 4 quads)
    // ─────────────────────────────────────────────────────────────────────────
    else if (segIdx < 52) {
        outSegType = 1.0;

        int bIdx = (segIdx - 36) / 4;   // branch index 0 .. 3
        int bSeg = (segIdx - 36) % 4;   // sub-segment 0 .. 3

        // Branch origins emerge directly from actual trunk joints!
        int trunkOriginJoint = 28 - bIdx * 6; // joints 28, 22, 16, 10
        vec3 forkOrigin = evalTrunkJointPos(trunkOriginJoint, gnd, cld, globalRight, globalFwd, seed, maxJag);

        float bSeed = seed + float(bIdx) * 11.37;
        float side = (bIdx % 2 == 0) ? 1.0 : -1.0;

        // Natural downward & lateral branch direction (river delta / tree fork)
        vec3 branchDir;
        if (strikeType == 1) {
            // Anvil crawler: spreads horizontally across sky
            float hAngle = (hash(bSeed, 1.1) - 0.5) * 3.14159;
            branchDir = normalize(globalRight * cos(hAngle) + globalFwd * sin(hAngle) + vec3(0.0, (hash(bSeed, 2.2) - 0.5) * 0.20, 0.0));
        } else {
            // Cloud-to-ground: branches fork downward and outward at ~30 deg
            float vertDown = mix(0.55, 0.85, hash(bSeed, 3.3));
            branchDir = normalize(-boltDir * vertDown + globalRight * (side * 0.65) + globalFwd * ((hash(bSeed, 4.4) - 0.5) * 0.45));
        }

        float branchLen = spanLen * mix(0.18, 0.35, hash(bSeed, 5.5));

        // Sub-segment joint index: 0..4
        int jb = (cornerUV.y > 0.5) ? (bSeg + 1) : bSeg;
        outUV = vec2(cornerUV.x, (float(bSeg) + cornerUV.y) / 4.0);

        vec3 pJoint = evalBranchJointPos(jb, forkOrigin, branchDir, globalRight, globalFwd, bSeed, branchLen, maxJag);

        // Branch billboard right vector
        int prevJ = max(0, jb - 1);
        int nextJ = min(4, jb + 1);
        vec3 pPrev = evalBranchJointPos(prevJ, forkOrigin, branchDir, globalRight, globalFwd, bSeed, branchLen, maxJag);
        vec3 pNext = evalBranchJointPos(nextJ, forkOrigin, branchDir, globalRight, globalFwd, bSeed, branchLen, maxJag);
        vec3 jTangent = normalize(pNext - pPrev);

        vec3 toCam = normalize(pc.cameraPos.xyz - pJoint);
        vec3 jRight = normalize(cross(jTangent, toCam));
        if (length(jRight) < 0.001) jRight = globalRight;

        // Branch width tapers smoothly to zero at tip
        float frac = float(jb) / 4.0;
        float hw = trunkHalf * mix(0.40, 0.02, frac);

        worldPos = pJoint + jRight * ((cornerUV.x - 0.5) * hw * 2.0);
    }
    // ─────────────────────────────────────────────────────────────────────────
    // 3. Secondary Tendrils & Cloud Crawlers (Segments 52 .. 63: 4 tendrils × 3 quads)
    // ─────────────────────────────────────────────────────────────────────────
    else {
        outSegType = 2.0;

        int tIdx = (segIdx - 52) / 3;   // tendril index 0 .. 3
        int tSeg = (segIdx - 52) % 3;   // sub-segment 0 .. 2
        float tSeed = seed + float(tIdx) * 17.53;

        vec3 tOrigin;
        vec3 tDir;
        float tLen;

        if (tIdx < 2) {
            // Sub-branches emerging from primary branches 0 and 1
            int parentB = tIdx;
            int trunkJoint = 28 - parentB * 6;
            vec3 parentFork = evalTrunkJointPos(trunkJoint, gnd, cld, globalRight, globalFwd, seed, maxJag);
            float pSeed = seed + float(parentB) * 11.37;
            float side = (parentB % 2 == 0) ? 1.0 : -1.0;
            vec3 pDir = normalize(-boltDir * 0.70 + globalRight * (side * 0.65) + globalFwd * ((hash(pSeed, 4.4) - 0.5) * 0.45));
            float pLen = spanLen * mix(0.18, 0.35, hash(pSeed, 5.5));
            // Fork from joint 2 of parent branch
            tOrigin = evalBranchJointPos(2, parentFork, pDir, globalRight, globalFwd, pSeed, pLen, maxJag);
            float subSide = -side;
            tDir = normalize(-boltDir * 0.65 + globalRight * (subSide * 0.75) + globalFwd * ((hash(tSeed, 1.9) - 0.5) * 0.5));
            tLen = spanLen * mix(0.08, 0.16, hash(tSeed, 2.9));
        } else {
            // Cloud crawlers spreading high in thunderhead
            int cJoint = 34 - (tIdx - 2) * 2;
            tOrigin = evalTrunkJointPos(cJoint, gnd, cld, globalRight, globalFwd, seed, maxJag);
            float angle = float(tIdx) * 1.57 + hash(tSeed, 3.1) * 0.8;
            tDir = normalize(globalRight * cos(angle) + globalFwd * sin(angle) + vec3(0.0, 0.15, 0.0));
            tLen = spanLen * mix(0.12, 0.25, hash(tSeed, 4.1));
        }

        int jt = (cornerUV.y > 0.5) ? (tSeg + 1) : tSeg;
        outUV = vec2(cornerUV.x, (float(tSeg) + cornerUV.y) / 3.0);

        vec3 pJoint = evalTendrilJointPos(jt, tOrigin, tDir, globalRight, globalFwd, tSeed, tLen, maxJag);

        int prevJ = max(0, jt - 1);
        int nextJ = min(3, jt + 1);
        vec3 pPrev = evalTendrilJointPos(prevJ, tOrigin, tDir, globalRight, globalFwd, tSeed, tLen, maxJag);
        vec3 pNext = evalTendrilJointPos(nextJ, tOrigin, tDir, globalRight, globalFwd, tSeed, tLen, maxJag);
        vec3 jTangent = normalize(pNext - pPrev);

        vec3 toCam = normalize(pc.cameraPos.xyz - pJoint);
        vec3 jRight = normalize(cross(jTangent, toCam));
        if (length(jRight) < 0.001) jRight = globalRight;

        float frac = float(jt) / 3.0;
        float hw = trunkHalf * mix(0.22, 0.01, frac);

        worldPos = pJoint + jRight * ((cornerUV.x - 0.5) * hw * 2.0);
    }

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
}
