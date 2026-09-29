#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUv;

void main() {
    vec4 worldPosition = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPosition;
    fragNormal = mat3(pc.model) * inNormal;
    fragUv = inUv;
}
