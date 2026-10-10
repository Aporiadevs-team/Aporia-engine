#version 450

// Shadow depth pass: renders the scene from the sun into the shadow map.
layout(push_constant) uniform PushConstants {
    mat4 sunViewProj;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = pc.sunViewProj * (pc.model * vec4(inPosition, 1.0));
}
