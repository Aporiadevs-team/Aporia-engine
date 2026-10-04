#version 450

// UI pass: SDF rounded rectangles with an optional in-rect blur. It shares the
// tonemap descriptor set (binding 0 = HDR scene) and blends over the already
// tonemapped frame with straight alpha, so no framebuffer fetch is needed.
layout(set = 0, binding = 0) uniform sampler2D hdrTexture;

layout(push_constant) uniform UiPush {
    vec4 rect;       // pixel x, y, width, height
    vec4 color;
    vec4 radius;     // TL, TR, BR, BL radius in pixels
    vec4 smoothing;  // x = derivative smoothing multiplier
    vec4 blur;       // x mode, y radius, z intensity
} ui;

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
    vec2 screen = vec2(textureSize(hdrTexture, 0));
    vec2 fragPos = fragUv * screen;

    vec2 center = ui.rect.xy + ui.rect.zw * 0.5;
    vec2 halfSize = ui.rect.zw * 0.5;
    vec4 r = min(ui.radius, vec4(min(halfSize.x, halfSize.y)));
    float radius = mix(mix(r.w, r.z, step(center.x, fragPos.x)),
                       mix(r.x, r.y, step(center.x, fragPos.x)),
                       step(center.y, fragPos.y));
    vec2 q = abs(fragPos - center) - halfSize + radius;
    float sdf = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float smoothing = max(ui.smoothing.x, 0.5);
    vec2 derivatives = vec2(dFdx(sdf), dFdy(sdf));
    float edge = max(length(derivatives) * smoothing, 0.5);
    float coverage = 1.0 - smoothstep(-edge, edge, sdf);

    if (coverage <= 0.0) {
        discard;
    }

    vec3 color = ui.color.rgb;
    float alpha = ui.color.a * coverage;
    if (ui.blur.x > 0.5) {
        // Panel shows the scene behind it, optionally blurred, tinted by the
        // panel color, then composited with straight alpha.
        vec3 scene = displaySample(fragUv);
        vec3 blurred = scene;
        float blurRadius = max(ui.blur.y, 1.0);
        vec2 o = texel * blurRadius;
        if (ui.blur.x < 1.5) {
            blurred = (displaySample(fragUv + vec2(o.x, o.y)) +
                       displaySample(fragUv + vec2(-o.x, o.y)) +
                       displaySample(fragUv + vec2(o.x, -o.y)) +
                       displaySample(fragUv + vec2(-o.x, -o.y)) +
                       scene) / 5.0;
        } else {
            blurred = scene * 0.227027;
            blurred += displaySample(fragUv + vec2(o.x, 0.0)) * 0.1945946;
            blurred += displaySample(fragUv - vec2(o.x, 0.0)) * 0.1945946;
            blurred += displaySample(fragUv + vec2(0.0, o.y)) * 0.1945946;
            blurred += displaySample(fragUv - vec2(0.0, o.y)) * 0.1945946;
        }
        blurred = mix(scene, blurred, clamp(ui.blur.z, 0.0, 1.0));
        color = mix(blurred, ui.color.rgb, ui.color.a);
        alpha = coverage;
    }

    outColor = vec4(color, alpha);
}
