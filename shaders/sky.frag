#version 450

// Procedural infinite sky (Z-up world). The view ray is rebuilt from the camera
// basis, so the sky is a pure function of the view direction. Colours are a
// vertical (altitude-based) three-zone gradient: burning horizon band -> mid
// sky -> zenith. Twilight adds a warm horizon band and a sun glow, night adds a
// moon and stars.
layout(set = 0, binding = 0) uniform SkyUniforms {
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraForward;
    vec4 cameraPosition;
    vec4 sunDirection;  // xyz toward the sun, w intensity (0 night .. 1 noon)
    vec4 sunColor;      // rgb colour, a unused
    vec4 skyParams;     // x horizon exponent, y haze, z glow, w tan(halfFovY)
    vec4 renderParams;  // x aspect
    vec4 twilight;      // rgb warm glow (dawn yellow / dusk pink), a intensity
} sky;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

float hash31(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.11, 0.17, 0.13));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec2 ndc = vec2(fragUv.x * 2.0 - 1.0, 1.0 - fragUv.y * 2.0);
    const float tanHalf = sky.skyParams.w;
    vec3 worldDir = normalize(
        sky.cameraForward.xyz +
        sky.cameraRight.xyz * (ndc.x * tanHalf * sky.renderParams.x) +
        sky.cameraUp.xyz * (ndc.y * tanHalf));

    const float elevationZ = clamp(worldDir.z, -1.0, 1.0);
    const float elevDeg = degrees(asin(elevationZ));
    const float daylight = clamp(sky.sunDirection.w, 0.0, 1.0);
    const float night = 1.0 - daylight;

    // Vertical three-zone gradient (day vs night blends by daylight).
    vec3 zenithDay = vec3(0.030, 0.070, 0.180), zenithNight = vec3(0.006, 0.010, 0.025);
    vec3 midDay = vec3(0.220, 0.380, 0.620), midNight = vec3(0.010, 0.016, 0.040);
    vec3 horizonDay = vec3(0.620, 0.720, 0.900), horizonNight = vec3(0.020, 0.026, 0.048);
    vec3 zenith = mix(zenithNight, zenithDay, daylight);
    vec3 mid = mix(midNight, midDay, daylight);
    vec3 horizon = mix(horizonNight, horizonDay, daylight);

    const float a = clamp(elevDeg, -90.0, 90.0);
    vec3 color;
    if (a >= 0.0) {
        vec3 lower = mix(horizon, mid, clamp(a / 30.0, 0.0, 1.0));
        color = mix(lower, zenith, clamp((a - 30.0) / 60.0, 0.0, 1.0));
    } else {
        color = mix(horizon, horizonNight * 0.6, clamp(-a / 25.0, 0.0, 1.0));
    }

    vec3 sunDir = normalize(sky.sunDirection.xyz);
    float cosAngle = dot(worldDir, sunDir);

    // Burning band hugging the horizon, strongest during twilight. Altitude-only,
    // so it is a horizontal strip (never diagonal).
    float band = exp(-pow(max(a, 0.0) / 6.0, 2.0));
    float burn = band * sky.twilight.a;
    color = mix(color, sky.twilight.rgb * 1.15, clamp(burn, 0.0, 0.85));

    // Compact twilight glow around the sun so the disk sinks rather than floats.
    float sunProx = pow(max(cosAngle, 0.0), 6.0);
    color = mix(color, sky.twilight.rgb, clamp(sky.twilight.a * sunProx * 0.7, 0.0, 0.8));

    // Sun disk + tight glow.
    float disk = smoothstep(0.9990, 0.9997, cosAngle);
    float glow = pow(max(cosAngle, 0.0), 400.0) * 0.5 + pow(max(cosAngle, 0.0), 32.0) * 0.10;
    color += sky.sunColor.rgb * (disk + glow) * sky.sunDirection.w * sky.skyParams.z;

    // Night: moon opposite the sun, plus faint stars.
    vec3 moonDir = normalize(-sunDir);
    float moonCos = dot(worldDir, moonDir);
    float moonDisk = smoothstep(0.9993, 0.9998, moonCos);
    float moonGlow = pow(max(moonCos, 0.0), 180.0) * 0.25;
    color += vec3(0.86, 0.89, 0.96) * (moonDisk + moonGlow) * night;
    if (night > 0.05 && elevationZ > 0.0) {
        float stars = hash31(floor(worldDir * 320.0));
        stars = smoothstep(0.9975, 1.0, stars);
        color += vec3(stars) * night;
    }

    outColor = vec4(color, 1.0); // linear HDR; the tonemap pass converts to display
}
