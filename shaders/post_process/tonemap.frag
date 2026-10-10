#version 450
#extension GL_GOOGLE_include_directive : require

// HDR -> display: ACES + morphological AA, with the post effects (bloom, god
// rays, froxel fog) pulled in from post_process/*.glsl. UI, TAA and the scene
// live in their own passes.
layout(set = 0, binding = 0) uniform sampler2D hdrTexture;
#if defined(APORIA_EXPERIMENTAL)
layout(set = 0, binding = 1) uniform sampler2D depthTexture;  // stored scene depth
layout(set = 0, binding = 2) uniform sampler3D froxelVolume;  // in-scattered fog grid
layout(set = 0, binding = 3) uniform VolumetricUniforms {
    mat4 invViewProj;
    vec4 cameraPosition; // xyz eye
    vec4 vparams;        // x near, y far, z maxDistance, w steps
    vec4 vparams2;       // x strength, y enabled
} vol;
#endif

layout(push_constant) uniform TonemapPush {
    vec4 rect;      // unused; kept so the layout matches the UI pass
    vec4 color;
    vec4 radius;
    vec4 smoothing;
    vec4 blur;
    vec4 aa;        // x = strength 0..1, y = mode (0 none, 1 SMAA)
    vec4 rays;      // x,y = sun screen uv, z = strength, w = visibility
    vec4 rays2;     // x = samples, y = decay, z = warm tint
    vec4 bloom;     // x = strength, y = threshold, z = radius (texels)
} params;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "bloom.glsl"
#include "fog.glsl"
#include "godrays.glsl"
#include "aa.glsl"

void main() {
    vec3 resolved = resolveAa(fragUv, displaySample(fragUv));
    resolved += pow(acesFilm(bloomGlow(fragUv)), vec3(1.0 / 2.2));
#if defined(APORIA_EXPERIMENTAL)
    resolved += pow(acesFilm(froxelFog(fragUv)), vec3(1.0 / 2.2));
#endif
    resolved += godRays(fragUv);
    outColor = vec4(resolved, 1.0);
}
