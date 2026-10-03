#version 450

// TTF bitmap text: coverage lives in the alpha channel, so the fragment stage
// plain-samples alpha. Outline is approximated by dilating coverage with a
// small cross of taps (no signed distance in a coverage atlas).

layout(set = 0, binding = 1) uniform sampler2D fontTexture;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec4 charColor;
layout(location = 2) in float outlineWidth;
layout(location = 3) in vec4 outlineColorIn;
layout(location = 4) in float pxRange;
layout(location = 5) in vec2 atlasSize;

layout(location = 0) out vec4 fragColor;

float coverage(vec2 uv) {
    return texture(fontTexture, uv).a;
}

void main() {
    float cov = coverage(texCoord);

    if (outlineWidth > 0.0) {
        vec2 texel = vec2(outlineWidth * 0.004);
        float dil = max(
            max(coverage(texCoord + vec2(texel.x, 0.0)), coverage(texCoord + vec2(-texel.x, 0.0))),
            max(coverage(texCoord + vec2(0.0, texel.y)), coverage(texCoord + vec2(0.0, -texel.y))));

        vec4 fill = vec4(charColor.rgb, charColor.a * cov);
        vec4 outline = vec4(outlineColorIn.rgb, outlineColorIn.a * max(0.0, dil - cov));
        fragColor = fill + outline * (1.0 - fill.a);
    } else {
        fragColor = vec4(charColor.rgb, charColor.a * cov);
    }

    if (fragColor.a < 0.004) {
        discard;
    }
}
