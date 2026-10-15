#version 450

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    mat4 viewProj; // Holds lightSpaceMatrix during shadow pass
    vec4 camPos;
    float scale;
    float fade;
} push;

void main() {
    gl_Position = push.viewProj * push.model * vec4(inPos, 1.0);
}
