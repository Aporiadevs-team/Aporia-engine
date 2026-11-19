// Morphological edge AA (SMAA/FXAA class): detect a luma edge, derive its
// direction from the 2x2 neighbourhood, then blend along it.
vec3 resolveAa(vec2 uv, vec3 resolved) {
    const vec2 texel = 1.0 / vec2(textureSize(hdrTexture, 0));
    const int mode = int(params.aa.y + 0.5);
    const float strength = clamp(params.aa.x, 0.0, 1.0);
    if (mode != 1 || strength <= 0.001) {
        return resolved;
    }
    const vec3 lumaWeight = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(displaySample(uv + vec2(-texel.x, -texel.y)), lumaWeight);
    float lumaNE = dot(displaySample(uv + vec2(texel.x, -texel.y)), lumaWeight);
    float lumaSW = dot(displaySample(uv + vec2(-texel.x, texel.y)), lumaWeight);
    float lumaSE = dot(displaySample(uv + vec2(texel.x, texel.y)), lumaWeight);
    float lumaCenter = dot(resolved, lumaWeight);
    float lumaMin = min(lumaCenter, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaCenter, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (lumaMax - lumaMin <= max(0.03, lumaMax * 0.08)) {
        return resolved;
    }
    vec2 direction;
    direction.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    direction.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.0625, 1.0 / 128.0);
    float inverse = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * inverse, vec2(-8.0), vec2(8.0)) * texel;
    vec3 rgbA = 0.5 * (
        displaySample(uv + direction * (1.0 / 3.0 - 0.5)) +
        displaySample(uv + direction * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        displaySample(uv + direction * -0.5) +
        displaySample(uv + direction * 0.5));
    float lumaB = dot(rgbB, lumaWeight);
    vec3 antiAliased = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    return mix(resolved, antiAliased, strength);
}
