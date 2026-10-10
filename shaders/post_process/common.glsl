// Shared HDR -> display helpers.

vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 displaySample(vec2 uv) {
    vec3 mapped = acesFilm(texture(hdrTexture, clamp(uv, 0.0, 1.0)).rgb * 0.9);
    return pow(mapped, vec3(1.0 / 2.2));
}
