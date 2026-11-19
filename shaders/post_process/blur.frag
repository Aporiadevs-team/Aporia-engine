#version 450

// Separable Gaussian blur (two passes per frame). xy = one target texel along
// the axis, z = radius in texels.
layout(set = 0, binding = 0) uniform sampler2D srcTexture;

layout(push_constant) uniform BlurPush {
    vec4 direction;
} blur;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 step = blur.direction.xy;
    const int maxRadius = 16;
    int radius = int(clamp(blur.direction.z, 1.0, float(maxRadius)) + 0.5);
    float sigma = max(float(radius) / 2.0, 0.5);
    float twoSigmaSq = 2.0 * sigma * sigma;

    vec3 sum = texture(srcTexture, fragUv).rgb;
    float total = 1.0;
    for (int i = 1; i <= maxRadius; ++i) {
        if (i > radius) {
            break;
        }
        float w = exp(-float(i * i) / twoSigmaSq);
        sum += texture(srcTexture, fragUv + step * float(i)).rgb * w;
        sum += texture(srcTexture, fragUv - step * float(i)).rgb * w;
        total += 2.0 * w;
    }
    outColor = vec4(sum / total, 1.0);
}
