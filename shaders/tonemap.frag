#version 450

// HDR -> display only: ACES approximation with a live morphological AA resolve.
// UI (SDF rects, blur), TAA and the scene itself live in their own passes.
layout(set = 0, binding = 0) uniform sampler2D hdrTexture;

layout(push_constant) uniform TonemapPush {
    vec4 rect;      // unused here; kept so the shared layout matches the UI pass
    vec4 color;
    vec4 radius;
    vec4 smoothing;
    vec4 blur;
    vec4 aa;        // x = strength 0..1, y = mode (0 none, 1 SMAA, 2 TAA)
} params;

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

vec3 displaySample(vec2 uv) {
    vec3 mapped = acesFilm(texture(hdrTexture, clamp(uv, 0.0, 1.0)).rgb * 1.1);
    return pow(mapped, vec3(1.0 / 2.2));
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(hdrTexture, 0));
    vec3 resolved = displaySample(fragUv);

    const int mode = int(params.aa.y + 0.5);
    const float strength = clamp(params.aa.x, 0.0, 1.0);
    if (mode == 1 && strength > 0.001) {
        // Morphological edge AA (SMAA/FXAA class): detect a luma edge, derive
        // its direction from the 2x2 neighborhood, then blend along it.
        const vec3 lumaWeight = vec3(0.299, 0.587, 0.114);
        float lumaNW = dot(displaySample(fragUv + vec2(-texel.x, -texel.y)), lumaWeight);
        float lumaNE = dot(displaySample(fragUv + vec2(texel.x, -texel.y)), lumaWeight);
        float lumaSW = dot(displaySample(fragUv + vec2(-texel.x, texel.y)), lumaWeight);
        float lumaSE = dot(displaySample(fragUv + vec2(texel.x, texel.y)), lumaWeight);
        float lumaCenter = dot(resolved, lumaWeight);
        float lumaMin = min(lumaCenter, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
        float lumaMax = max(lumaCenter, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
        if (lumaMax - lumaMin > max(0.03, lumaMax * 0.08)) {
            vec2 direction;
            direction.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
            direction.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
            float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.0625, 1.0 / 128.0);
            float inverse = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
            direction = clamp(direction * inverse, vec2(-8.0), vec2(8.0)) * texel;
            vec3 rgbA = 0.5 * (
                displaySample(fragUv + direction * (1.0 / 3.0 - 0.5)) +
                displaySample(fragUv + direction * (2.0 / 3.0 - 0.5)));
            vec3 rgbB = rgbA * 0.5 + 0.25 * (
                displaySample(fragUv + direction * -0.5) +
                displaySample(fragUv + direction * 0.5));
            float lumaB = dot(rgbB, lumaWeight);
            vec3 antiAliased = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
            resolved = mix(resolved, antiAliased, strength);
        }
    }

    outColor = vec4(resolved, 1.0);
}
