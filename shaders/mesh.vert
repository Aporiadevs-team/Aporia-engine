#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 model;
    vec4 color;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUv;
layout(location = 5) out vec4 fragColor;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) out vec4 fragShadowPosition;
layout(location = 4) out vec4 fragClipPosition;

layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 sunViewProj;
    vec4 sunDirection;
    vec4 cameraPosition;
    vec4 shadingParams;
    vec4 screenParams;
} scene;

void main() {
    vec4 worldPosition = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPosition;
    fragNormal = normalize(mat3(pc.model) * inNormal);
    fragUv = inUv;
    fragColor = pc.color;
    fragWorldPosition = worldPosition.xyz;
    fragShadowPosition = scene.sunViewProj * worldPosition;
    fragClipPosition = gl_Position;
}
