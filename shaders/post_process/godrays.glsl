float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Screen-space crepuscular rays: march each pixel toward the sun and accumulate
// the bright (sun) samples from the HDR image. Geometry occludes the source, so
// shafts only appear where the sun is visible. Jittered march start removes
// banding; the accumulated HDR is tonemapped and warmed for a rich glow.
vec3 godRays(vec2 uv) {
    const float visible = params.rays.w;
    const float strength = params.rays.z;
    if (visible < 0.001 || strength < 0.001) {
        return vec3(0.0);
    }
    const int samples = int(params.rays2.x + 0.5);
    const float decay = params.rays2.y;
    const float warm = clamp(params.rays2.z, 0.0, 1.0);
    const vec2 sunUv = params.rays.xy;
    const vec2 toSun = sunUv - uv;
    const vec2 stepUv = toSun / float(samples);
    vec2 coord = uv + stepUv * hash12(uv * 2048.0);
    vec3 accum = vec3(0.0);
    float weight = 1.0;
    for (int index = 0; index < 96; ++index) {
        if (index >= samples) {
            break;
        }
        coord += stepUv;
        vec3 sampleColor = texture(hdrTexture, clamp(coord, 0.0, 1.0)).rgb; // linear HDR
        float lum = dot(sampleColor, vec3(0.299, 0.587, 0.114));
        float source = smoothstep(0.5, 2.5, lum); // isolate the bright sun
        accum += sampleColor * source * weight;
        weight *= decay;
    }
    accum /= float(samples);
    vec3 rayColor = pow(acesFilm(accum * 1.25), vec3(1.0 / 2.2));
    rayColor = mix(rayColor, rayColor * vec3(1.18, 0.97, 0.78), warm);
    float radial = 1.0 - clamp(length(toSun) * 1.3, 0.0, 1.0);
    return rayColor * strength * visible * (0.25 + 0.75 * radial);
}
