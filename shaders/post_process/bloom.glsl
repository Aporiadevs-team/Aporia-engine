// HDR bloom: bright-pass the high-range picture and blur it over several rings,
// so highlights (specular glints, sun-lit surfaces, lamps) spread a soft glow.
vec3 bloomGlow(vec2 uv) {
    const float strength = params.bloom.x;
    if (strength < 0.001) {
        return vec3(0.0);
    }
    const float threshold = params.bloom.y;
    const float radius = params.bloom.z;
    const vec2 texel = 1.0 / vec2(textureSize(hdrTexture, 0));
    vec3 accum = vec3(0.0);
    const int rings = 5;
    const int perRing = 8;
    for (int ring = 1; ring <= rings; ++ring) {
        const float rr = float(ring) / float(rings) * radius;
        for (int s = 0; s < perRing; ++s) {
            const float a = 6.2831853 * (float(s) / float(perRing)) + float(ring) * 0.9;
            vec3 c = texture(hdrTexture, clamp(uv + vec2(cos(a), sin(a)) * rr * texel, 0.0, 1.0)).rgb;
            float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
            c *= max(lum - threshold, 0.0) / max(lum, 1.0e-4); // keep only the over-white part
            accum += c;
        }
    }
    return accum / float(rings * perRing) * strength;
}
