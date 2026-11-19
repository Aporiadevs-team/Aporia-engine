#if defined(APORIA_EXPERIMENTAL)
// Froxel fog: march the pre-lit grid along the view ray to the stored depth.
// A world point on this pixel's ray reprojects to the same pixel, so xy = uv.
vec3 froxelFog(vec2 uv) {
    if (vol.vparams2.y < 0.5) {
        return vec3(0.0);
    }
    const int steps = int(max(vol.vparams.w, 1.0));
    const float near = vol.vparams.x;
    const float far = max(vol.vparams.y, near + 1.0e-3);
    const float maxDistance = vol.vparams.z;

    const float depth = texture(depthTexture, uv).r;
    const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const vec4 world = vol.invViewProj * vec4(ndc, depth, 1.0);
    const vec3 worldPos = world.xyz / world.w;
    const vec3 cameraPos = vol.cameraPosition.xyz;
    const float surfaceDistance = length(worldPos - cameraPos);
    const float marchDistance = min(surfaceDistance, maxDistance);
    if (marchDistance <= near) {
        return vec3(0.0);
    }

    const float stepLength = marchDistance / float(steps);
    const float lnRatio = log(far / near);
    const float jitter = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
    vec3 accum = vec3(0.0);
    float transmittance = 1.0;
    for (int index = 0; index < 96; ++index) {
        if (index >= steps) {
            break;
        }
        const float t = (float(index) + jitter) * stepLength;
        const float slice = clamp(log(max(t, near) / near) / lnRatio, 0.0, 1.0);
        const vec4 froxel = texture(froxelVolume, vec3(uv, slice));
        accum += transmittance * froxel.rgb * stepLength;
        transmittance *= exp(-froxel.a * stepLength);
        if (transmittance < 0.02) {
            break;
        }
    }
    return accum * vol.vparams2.x;
}
#endif
