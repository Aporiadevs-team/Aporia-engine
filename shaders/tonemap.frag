#version 450

// HDR -> display: ACES approximation, exposure baked in. Linear input from
// the R16G16B16A16_HDR target, sRGB-ish output for the swapchain.
layout(set = 0, binding = 0) uniform sampler2D hdrTexture;
layout(constant_id = 0) const int AA_MODE = 1; // 0 none, 1 SMAA-style edge neighborhood, 2 TAA placeholder

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
    vec3 hdr = texture(hdrTexture, fragUv).rgb;
    vec2 screen = vec2(textureSize(hdrTexture, 0));
    vec2 fragPos = fragUv * screen;
    bool insideRect = all(greaterThanEqual(fragPos, ui.rect.xy)) &&
                      all(lessThanEqual(fragPos, ui.rect.xy + ui.rect.zw));
    const vec3 sourceDisplay = displaySample(fragUv);
    vec3 resolved = sourceDisplay;
    if (insideRect && ui.blur.x > 0.5) {
        float r = max(ui.blur.y, 1.0);
        if (ui.blur.x < 1.5) {
            vec2 o = texel * r;
            resolved = (
                displaySample(fragUv + vec2(o.x, o.y)) +
                displaySample(fragUv + vec2(-o.x, o.y)) +
                displaySample(fragUv + vec2(o.x, -o.y)) +
                displaySample(fragUv + vec2(-o.x, -o.y)) +
                sourceDisplay) / 5.0;
        } else {
            vec2 o = texel * r;
            resolved = sourceDisplay * 0.227027;
            resolved += displaySample(fragUv + vec2(o.x, 0.0)) * 0.1945946;
            resolved += displaySample(fragUv - vec2(o.x, 0.0)) * 0.1945946;
            resolved += displaySample(fragUv + vec2(0.0, o.y)) * 0.1945946;
            resolved += displaySample(fragUv - vec2(0.0, o.y)) * 0.1945946;
        }
        resolved = mix(sourceDisplay, resolved, clamp(ui.blur.z, 0.0, 1.0));
    }
    if (AA_MODE == 1) {
        // Lightweight SMAA-style neighborhood resolve. It detects a local
        // luminance edge and blends four diagonal taps only near that edge.
        float center = dot(resolved, vec3(0.299, 0.587, 0.114));
        float left = dot(displaySample(fragUv - vec2(texel.x, 0.0)), vec3(0.299, 0.587, 0.114));
        float right = dot(displaySample(fragUv + vec2(texel.x, 0.0)), vec3(0.299, 0.587, 0.114));
        float up = dot(displaySample(fragUv - vec2(0.0, texel.y)), vec3(0.299, 0.587, 0.114));
        float down = dot(displaySample(fragUv + vec2(0.0, texel.y)), vec3(0.299, 0.587, 0.114));
        float edge = smoothstep(0.04, 0.22, max(max(abs(center - left), abs(center - right)), max(abs(center - up), abs(center - down))));
        vec3 neighborhood = 0.25 * (displaySample(fragUv + vec2(-texel.x, -texel.y)) +
            displaySample(fragUv + vec2(texel.x, -texel.y)) +
            displaySample(fragUv + vec2(-texel.x, texel.y)) +
            displaySample(fragUv + vec2(texel.x, texel.y)));
        resolved = mix(resolved, neighborhood, edge * 0.65);
    }
    // Pixel-space roundedBoxSDF adapted from the engine's reference UI shader.
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
    bool inside = insideRect;
    outColor = vec4(mix(resolved, ui.color.rgb, ui.color.a * coverage), 1.0);
}
