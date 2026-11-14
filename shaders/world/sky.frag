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
    vec4 seasonSky;     // rgb zenith tint, a tint strength (0 = default palette)
    vec4 seasonHaze;    // rgb horizon tint
    vec4 fog;           // rgb weather fog colour, a horizon fog blend (0 = off)
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

    // Vertical three-zone gradient (day vs night blends by daylight). Season
    // tints scale the day palette; a zero strength leaves it untouched.
    vec3 zenithDay = vec3(0.030, 0.070, 0.180), zenithNight = vec3(0.006, 0.010, 0.025);
    vec3 midDay = vec3(0.220, 0.380, 0.620), midNight = vec3(0.010, 0.016, 0.040);
    vec3 horizonDay = vec3(0.620, 0.720, 0.900), horizonNight = vec3(0.020, 0.026, 0.048);
    const float seasonStrength = clamp(sky.seasonSky.a, 0.0, 1.0);
    const vec3 zenithTint = mix(vec3(1.0), sky.seasonSky.rgb, seasonStrength);
    const vec3 horizonTint = mix(vec3(1.0), sky.seasonHaze.rgb, seasonStrength);
    const vec3 midTint = mix(zenithTint, horizonTint, 0.5);
    zenithDay *= zenithTint;
    midDay *= midTint;
    horizonDay *= horizonTint;
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

    // Twilight glow hugging the sun (kept tight so it does not flood the sky).
    float sunProx = pow(max(cosAngle, 0.0), 22.0);
    color = mix(color, sky.twilight.rgb, clamp(sky.twilight.a * sunProx * 0.5, 0.0, 0.6));

    // Sun disk + a tight glow (no wide halo).
    float disk = smoothstep(0.9995, 0.9998, cosAngle);
    float glow = pow(max(cosAngle, 0.0), 400.0) * 0.5 + pow(max(cosAngle, 0.0), 96.0) * 0.06;
    color += sky.sunColor.rgb * (disk + glow) * sky.sunDirection.w * sky.skyParams.z;

    // Sun corona: a bright core, a soft glare halo and four thin rays (a lens
    // star), like the reference's lamp coronas. Offset in camera right/up space.
    {
        const float lx = dot(worldDir, sky.cameraRight.xyz);
        const float ly = dot(worldDir, sky.cameraUp.xyz);
        const float sx = dot(sunDir, sky.cameraRight.xyz);
        const float sy = dot(sunDir, sky.cameraUp.xyz);
        const float r = length(vec2(lx - sx, ly - sy));
        // Round glow only -- no cross/star on the sun. The beams that reach the
        // camera come from the screen-space god rays in the tonemap pass.
        const float core = exp(-r * r / 0.00035) * 1.6 + exp(-r * 10.0) * 0.12;
        const vec3 coronaColor = mix(sky.sunColor.rgb, vec3(1.0, 0.9, 0.7), 0.5);
        color += coronaColor * core * sky.sunDirection.w;
    }

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

    // Weather fog collects toward the horizon so the sky matches the fogged
    // world without flattening the zenith.
    if (sky.fog.a > 0.0) {
        const float horizonWeight = 0.35 + 0.65 * (1.0 - clamp(elevationZ, 0.0, 1.0));
        color = mix(color, sky.fog.rgb, clamp(sky.fog.a * horizonWeight, 0.0, 0.9));
    }

    outColor = vec4(color, 1.0); // linear HDR; the tonemap pass converts to display
}
