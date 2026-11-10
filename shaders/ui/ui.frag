#version 450

// UI pass: SDF rounded rectangles with an optional in-rect blur. It shares the
// tonemap descriptor set (binding 0 = HDR scene) and blends over the already
// tonemapped frame with straight alpha, so no framebuffer fetch is needed.
// NOTE: No ACES tonemapping here — the scene is already tonemapped in the
// tonemap pass. Applying acesFilm() a second time would crush highlights.
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

// displaySample is kept only for the blur path which composites a display-space
// blur over the linear HDR scene before the tonemap pass runs again.  The blur
// path (ui.blur.x > 0.5) reads hdrTexture in linear space and mixes it with
// the solid color; for that path we do need a single display conversion so the
// blurred result doesn't look washed out.
vec3 displaySample(vec2 uv) {
    vec3 linear = texture(hdrTexture, clamp(uv, 0.0, 1.0)).rgb * 1.1;
    vec3 mapped = pow(linear, vec3(1.0 / 2.2));
    return mapped;
}

void main() {
    // Window-space position (top-left origin). Not textureSize(): with blur live,
    // binding 0 is the half-res target and would halve the coordinate space.
    vec2 fragPos = gl_FragCoord.xy;

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
        // smoothing.y: 0 = linear HDR blur (tonemap it), 1 = display-space composite.
        vec3 blurred = ui.smoothing.y > 0.5
            ? texture(hdrTexture, clamp(fragUv, 0.0, 1.0)).rgb
            : displaySample(fragUv);
        color = mix(blurred, ui.color.rgb, ui.color.a);
        alpha = coverage;
    }

    outColor = vec4(color, alpha);
}
