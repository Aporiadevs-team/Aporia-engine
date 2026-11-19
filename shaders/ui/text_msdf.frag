#version 450

// MSDF text: median of RGB gives the signed distance, screen-space range from
// fwidth keeps edges crisp at any scale, outline expands the distance field.

layout(set = 0, binding = 1) uniform sampler2D fontTexture;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec4 charColor;
layout(location = 2) in float outlineWidth;
layout(location = 3) in vec4 outlineColorIn;
layout(location = 4) in float pxRange;
layout(location = 5) in vec2 atlasSize;

layout(location = 0) out vec4 fragColor;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float screenPxRange() {
    vec2 unitRange = vec2(pxRange) / atlasSize;
    vec2 screenTexSize = vec2(1.0) / fwidth(texCoord);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec3 msd = texture(fontTexture, texCoord).rgb;
    float sd = median(msd.r, msd.g, msd.b);

    float screenPxDist = screenPxRange() * (sd - 0.5);
    float opacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

    if (outlineWidth > 0.0) {
        float outlineDist = screenPxDist + outlineWidth;
        float outlineOpacity = clamp(outlineDist + 0.5, 0.0, 1.0);

        vec4 fill = vec4(charColor.rgb, charColor.a * opacity);
        vec4 outline = vec4(outlineColorIn.rgb, outlineColorIn.a * max(0.0, outlineOpacity - opacity));
        fragColor = fill + outline * (1.0 - fill.a);
    } else {
        fragColor = vec4(charColor.rgb, charColor.a * opacity);
    }

    if (fragColor.a < 0.004) {
        discard;
    }
}
