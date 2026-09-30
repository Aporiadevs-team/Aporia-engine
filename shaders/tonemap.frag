#version 450

// HDR -> display: ACES approximation, exposure baked in. Linear input from
// the R16G16B16A16_HDR target, sRGB-ish output for the swapchain.
layout(set = 0, binding = 0) uniform sampler2D hdrTexture;

layout(location = 0) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    const vec3 hdr = texture(hdrTexture, fragUv).rgb;
    const vec3 toneMapped = acesFilm(hdr * 1.1);
    const vec3 gamma = pow(toneMapped, vec3(1.0 / 2.2));
    outColor = vec4(gamma, 1.0);
}
