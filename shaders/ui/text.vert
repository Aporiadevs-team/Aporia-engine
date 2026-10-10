#version 450

// Uniform-driven text quads: no vertex buffer, each glyph is 6 vertices
// generated from gl_VertexIndex. FontData layout must match
// Aporia::graphics_internal::FontData in TextRenderer.hpp.

layout(set = 0, binding = 0) uniform FontData {
    vec4 screenData;    // xy = screen size, z = guiScale, w = outlineWidth
    vec4 outlineColor;  // rgba
    vec4 atlasData;     // xy = atlas size, z = distanceRange, w = fontSize
    ivec4 charCount;    // x = number of glyphs in batch
    vec4 chars[2048];    // 4 vec4 per glyph: posSize, uvCoords, color, rotationData
} font;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec4 charColor;
layout(location = 2) out float outlineWidth;
layout(location = 3) out vec4 outlineColorOut;
layout(location = 4) out float pxRange;
layout(location = 5) out vec2 atlasSize;

vec2 rotatePoint(vec2 point, vec2 pivot, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    vec2 p = point - pivot;
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c) + pivot;
}

void main() {
    int charIndex = gl_VertexIndex / 6;
    int vertexIndex = gl_VertexIndex % 6;

    vec4 posSize = font.chars[charIndex * 4];
    vec4 uvCoords = font.chars[charIndex * 4 + 1];
    vec4 color = font.chars[charIndex * 4 + 2];
    vec4 rotationData = font.chars[charIndex * 4 + 3];

    vec2 positions[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

    vec2 pos = positions[vertexIndex];
    vec2 screenPos = posSize.xy + pos * posSize.zw;
    if (rotationData.x != 0.0) {
        screenPos = rotatePoint(screenPos, rotationData.yz, rotationData.x);
    }

    // Vulkan NDC y already points down, matching the top-left screen space
    // used to lay glyphs out, so no y flip is needed.
    vec2 ndc = (screenPos / font.screenData.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    texCoord = vec2(mix(uvCoords.x, uvCoords.z, pos.x), mix(uvCoords.y, uvCoords.w, pos.y));
    charColor = color;
    outlineWidth = font.screenData.w;
    outlineColorOut = font.outlineColor;
    pxRange = font.atlasData.z;
    atlasSize = font.atlasData.xy;
}
