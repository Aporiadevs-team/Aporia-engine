#version 450

layout(set = 0, binding = 0) uniform sampler2D albedoTexture;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDirection = normalize(vec3(0.35, -0.5, 0.8));
    float diffuse = max(dot(normalize(fragNormal), lightDirection), 0.0);
    float ambient = 0.35;
    vec3 shaded = vec3(1.0) * (ambient + 0.65 * diffuse);
    outColor = texture(albedoTexture, fragUv) * vec4(shaded, 1.0);
}
