#version 450

// Shadow depth pass: renders the scene from the sun into the shadow map.
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 sunViewProj;
} scene;

void main() {
    gl_Position = scene.sunViewProj * (pc.model * vec4(inPosition, 1.0));
}
