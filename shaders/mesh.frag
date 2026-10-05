#version 450

// Mesh lighting: sun diffuse + shadows + reflection blend, output linear HDR.
// Quality tiers arrive as specialization constants, so one pipeline serves
// Low/Medium/High: PCF taps and reflection toggle compile per preset.
layout(constant_id = 0) const int SHADOW_TAPS = 9;   // 1 Low, 9 Medium (3x3), 49 High (7x7)
layout(constant_id = 1) const float REFLECTIONS_ON = 1.0;

layout(set = 0, binding = 0) uniform sampler2D albedoTexture;
layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform sampler2D reflectionTexture; // mirror plane texture (screen space)

#if defined(APORIA_EXPERIMENTAL)
// EXPERIMENTAL multi-layer material (set = 1). Layer 0 is the base; layers 1..3
// blend in through the mask0 channels.
layout(set = 1, binding = 0) uniform sampler2D layerDiffuse0;
layout(set = 1, binding = 1) uniform sampler2D layerDiffuse1;
layout(set = 1, binding = 2) uniform sampler2D layerDiffuse2;
layout(set = 1, binding = 3) uniform sampler2D layerDiffuse3;
layout(set = 1, binding = 4) uniform sampler2D layerMask0;
layout(set = 1, binding = 5) uniform sampler2D layerMask1;
// Per-layer UV transform: uv = fragUv * uvScale[i] + uvOffset[i].
layout(set = 1, binding = 6) uniform LayerParams {
    vec4 uvScale;
    vec4 uvOffset;
} layerParams;
#endif

layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 sunViewProj;     // world -> sun clip space
    vec4 sunDirection;    // xyz toward light, w intensity
    vec4 cameraPosition;  // xyz world, w unused
    vec4 shadingParams;   // x ambient, y shadow strength, z mirror z, w shadow map texel size
    vec4 screenParams;    // x,y viewport size, z,y unused
    vec4 shadowParams;    // filter, blocker samples, filter samples, contact distance
} scene;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUv;
layout(location = 5) in vec4 fragColor;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) in vec4 fragShadowPosition; // sun clip space
layout(location = 4) in vec4 fragClipPosition;   // main camera clip space

layout(location = 0) out vec4 outColor;

float shadowSample(vec2 uv, float depth, float compare) {
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 1.0;
    }
    return depth - 0.0015 <= texture(shadowMap, uv).r ? 1.0 : 0.0;
}

float pcfShadow(vec3 normal) {
    if (fragShadowPosition.w <= 0.0) {
        return 1.0; // behind the sun camera: outside the map, full light
    }
    vec3 projected = fragShadowPosition.xyz / fragShadowPosition.w;
    // Vulkan clip-space XY is [-1, 1], while the shadow sampler uses [0, 1].
    projected.xy = projected.xy * 0.5 + 0.5;
    if (any(lessThan(projected.xy, vec2(0.0))) || any(greaterThan(projected.xy, vec2(1.0)))) {
        return 1.0;
    }
    const float bias = max(0.00025 * (1.0 - dot(normal, scene.sunDirection.xyz)), 0.00005);
    const float depth = projected.z - bias;
    const float texel = scene.shadingParams.w;
    float filterMode = scene.shadowParams.x;
    if (filterMode > 0.5 && filterMode < 1.5) {
        // PCSS blocker search followed by a variable penumbra filter.
        float blockers = 0.0;
        float blockerDepth = 0.0;
        const int blockerCount = 16;
        for (int i = 0; i < blockerCount; ++i) {
            float a = 6.2831853 * (float(i) / float(blockerCount));
            vec2 sampleUv = projected.xy + vec2(cos(a), sin(a)) * texel * 3.0;
            float sampleDepth = texture(shadowMap, clamp(sampleUv, 0.0, 1.0)).r;
            if (sampleDepth < depth) {
                blockers += 1.0;
                blockerDepth += sampleDepth;
            }
        }
        float penumbra = blockers > 0.0 ? clamp((depth - blockerDepth / blockers) * 80.0, 1.0, 5.0) : 1.0;
        float lit = 0.0;
        const int filterCount = 32;
        for (int i = 0; i < filterCount; ++i) {
            float a = 6.2831853 * (float(i) / float(filterCount));
            vec2 sampleUv = projected.xy + vec2(cos(a), sin(a)) * texel * penumbra;
            lit += shadowSample(sampleUv, depth, 0.0);
        }
        return mix(scene.shadingParams.y, 1.0, lit / float(filterCount));
    }
    float lit = 0.0;
    const int radius = SHADOW_TAPS <= 1 ? 0 : (SHADOW_TAPS <= 9 ? 1 : 3);
    float total = 0.0;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            lit += shadowSample(projected.xy + vec2(float(dx), float(dy)) * texel, depth, 0.0);
            total += 1.0;
        }
    return mix(scene.shadingParams.y, 1.0, lit / total);
}

float contactShadow(vec3 normal) {
    float distanceLimit = scene.shadowParams.w;
    if (distanceLimit <= 0.0 || fragClipPosition.w <= 0.0) {
        return 1.0;
    }
    // Short screen-space march toward the sun direction. It only darkens
    // immediate contacts and leaves the sun shadow map responsible for broad
    // silhouettes.
    vec3 rayStart = fragWorldPosition + normal * 0.015;
    float visibility = 1.0;
    for (int i = 1; i <= 8; ++i) {
        float t = distanceLimit * (float(i) / 8.0);
        vec3 sampleWorld = rayStart + scene.sunDirection.xyz * t;
        vec4 clip = scene.sunViewProj * vec4(sampleWorld, 1.0);
        if (clip.w <= 0.0) continue;
        vec3 projected = clip.xyz / clip.w;
        projected.xy = projected.xy * 0.5 + 0.5;
        if (any(lessThan(projected.xy, vec2(0.0))) || any(greaterThan(projected.xy, vec2(1.0)))) continue;
        float receiverDepth = projected.z;
        float mapDepth = texture(shadowMap, projected.xy).r;
        if (mapDepth + 0.002 < receiverDepth) {
            visibility = min(visibility, 0.55);
        }
    }
    return visibility;
}

vec3 planarReflection(vec3 baseColor, vec3 normal, vec3 viewDirection) {
    // RenderItem does not identify mirror surfaces yet. Sampling the reflected
    // scene onto every fragment overlays an upside-down duplicate over the
    // world, so keep the ordinary scene stable until mirror materials exist.
    return baseColor;
}

void main() {
#if defined(APORIA_EXPERIMENTAL)
    // Multi-layer splat with per-layer UV transforms: asphalt + line + detail
    // blended by the mask channels. Masks themselves use the raw UV (lightmap).
    vec2 uv0 = fragUv * layerParams.uvScale.x + layerParams.uvOffset.x;
    vec2 uv1 = fragUv * layerParams.uvScale.y + layerParams.uvOffset.y;
    vec2 uv2 = fragUv * layerParams.uvScale.z + layerParams.uvOffset.z;
    vec2 uv3 = fragUv * layerParams.uvScale.w + layerParams.uvOffset.w;
    vec3 albedo = texture(layerDiffuse0, uv0).rgb;
    vec4 mask = texture(layerMask0, fragUv);
    albedo = mix(albedo, texture(layerDiffuse1, uv1).rgb, mask.r);
    albedo = mix(albedo, texture(layerDiffuse2, uv2).rgb, mask.g);
    albedo = mix(albedo, texture(layerDiffuse3, uv3).rgb, mask.b);
    albedo *= fragColor.rgb;
#else
    vec3 albedo = texture(albedoTexture, fragUv).rgb * fragColor.rgb;
#endif
    float albedoLuminance = dot(albedo, vec3(0.299, 0.587, 0.114));
    albedo = mix(albedo, vec3(albedoLuminance), 0.18);
    vec3 normal = normalize(fragNormal);

    float diffuse = max(dot(normal, scene.sunDirection.xyz), 0.0);
    float shadow = pcfShadow(normal) * contactShadow(normal);
    vec3 skyAmbient = vec3(0.12, 0.15, 0.22);
    vec3 sunlight = vec3(1.0, 0.96, 0.86) * diffuse * shadow * scene.sunDirection.w;
    vec3 lit = albedo * (skyAmbient * scene.shadingParams.x + sunlight);

    vec3 viewDirection = normalize(scene.cameraPosition.xyz - fragWorldPosition);
    lit = planarReflection(lit, normal, viewDirection);

    outColor = vec4(lit, fragColor.a); // linear HDR; tonemap pass converts to display
}
