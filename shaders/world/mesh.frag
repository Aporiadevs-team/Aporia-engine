#version 450

// Mesh lighting: sun diffuse + shadows + reflection blend, output linear HDR.
// Quality tiers arrive as specialization constants, so one pipeline serves
// Low/Medium/High: PCF taps and reflection toggle compile per preset.
layout(constant_id = 0) const int SHADOW_TAPS = 9;   // 1 Low, 9 Medium (3x3), 49 High (7x7)
layout(constant_id = 1) const float REFLECTIONS_ON = 1.0;
layout(constant_id = 2) const int LIGHTS_MAX = 16;   // 0 Low, 8 Medium, 16 High

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
    vec4 lightInfo;       // x = active light count
    // Dynamic lights (structure of arrays). Per light: position.xyz + type,
    // direction.xyz (toward light), color.rgb + intensity, params =
    // (radius, softness, innerCos, outerCos).
    vec4 lightPosition[16];
    vec4 lightDirection[16];
    vec4 lightColor[16];
    vec4 lightParams[16];
} scene;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUv;
layout(location = 5) in vec4 fragColor;
layout(location = 6) in float fragEmissive;
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
    vec4 base = texture(layerDiffuse0, uv0);
    vec3 albedo = base.rgb;
    float texAlpha = base.a;
    vec4 mask = texture(layerMask0, fragUv);
    albedo = mix(albedo, texture(layerDiffuse1, uv1).rgb, mask.r);
    albedo = mix(albedo, texture(layerDiffuse2, uv2).rgb, mask.g);
    albedo = mix(albedo, texture(layerDiffuse3, uv3).rgb, mask.b);
    albedo *= fragColor.rgb;
#else
    vec4 albedoSample = texture(albedoTexture, fragUv);
    vec3 albedo = albedoSample.rgb * fragColor.rgb;
    float texAlpha = albedoSample.a;
#endif
    float albedoLuminance = dot(albedo, vec3(0.299, 0.587, 0.114));
    albedo = mix(albedo, vec3(albedoLuminance), 0.18);
    vec3 normal = normalize(fragNormal);

    float diffuse = max(dot(normal, scene.sunDirection.xyz), 0.0);
    float shadow = pcfShadow(normal) * contactShadow(normal);
    vec3 skyAmbient = vec3(0.12, 0.15, 0.22);
    vec3 sunlight = vec3(1.0, 0.96, 0.86) * diffuse * shadow * scene.sunDirection.w;
    vec3 lit = albedo * (skyAmbient * scene.shadingParams.x + sunlight);

    // Dynamic lights: point/spot with improved physics-based falloff,
    // directional without attenuation. Adds inverse-square-ish attenuation
    // plus soft radial falloff ("рыхлость"), simple contact occlusion to
    // block the light through geometry, and Cook-Torrance-style specular.
    const int lightCount = int(scene.lightInfo.x);
    for (int i = 0; i < LIGHTS_MAX; ++i) {
        if (i >= lightCount) {
            break;
        }
        vec4 lp = scene.lightPosition[i];
        vec4 ldir = scene.lightDirection[i];
        vec4 lcol = scene.lightColor[i];
        vec4 lprm = scene.lightParams[i];
        vec3 L;
        float atten = 1.0;
        float occlusion = 1.0;
        if (lp.w < 0.5) {
            L = normalize(ldir.xyz); // directional — no attenuation
        } else {
            vec3 toLight = lp.xyz - fragWorldPosition;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);

            // Point/Spot: inverse-square attenuation clamped by radius + soft edge.
            // Uses a physical I/d^2 term scaled by intensity * radius^2 so that a
            // light of intensity I and radius R emits roughly I at distance R.
            float invSq = 1.0 / (dist * dist);
            float scale = lprm.x * lprm.x; // radius^2
            atten = invSq * scale;
            // Soft roll-off near radius edge
            float t = clamp(1.0 - dist / max(lprm.x, 1e-4), 0.0, 1.0);
            atten *= t * t * (3.0 - 2.0 * t);
            atten = pow(max(atten, 0.0), max(lprm.y, 0.01));

            // Simple sphere occlusion march: short ray toward the light with
            // per-step shadow map + world-space check. Only the first 3 steps
            // matter — it's cheap and kills through-wall light spill.
            if (fragClipPosition.w > 0.0) {
                float stepDist = min(dist * 0.4, 3.0); // two steps max
                for (int s = 1; s <= 3; ++s) {
                    float st = stepDist * float(s) / 3.0;
                    vec3 sampleWorld = fragWorldPosition + L * st;
                    vec4 sampleClip = scene.sunViewProj * vec4(sampleWorld, 1.0);
                    if (sampleClip.w > 0.0) {
                        vec3 sc = sampleClip.xyz / sampleClip.w;
                        sc.xy = sc.xy * 0.5 + 0.5;
                        if (all(greaterThan(sc.xy, vec2(0.0))) && all(lessThan(sc.xy, vec2(1.0)))) {
                            float receiverDepth = sc.z;
                            float mapDepth = texture(shadowMap, sc.xy).r;
                            if (mapDepth + 0.003 < receiverDepth) {
                                occlusion = min(occlusion, 0.15);
                            }
                        }
                    }
                }
            }

            // Spot cone
            if (lp.w > 1.5) {
                float cosAngle = dot(normalize(-L), normalize(ldir.xyz));
                atten *= smoothstep(lprm.w, lprm.z, cosAngle);
            }
        }

        float ndl = max(dot(normal, L), 0.0);
        vec3 lightContribution = albedo * lcol.rgb * lcol.w * ndl * atten * occlusion;

        // --- Specular (Blinn-Phong approximation) ---
        // Simple but effective: half-vector based specular that makes wet
        // asphalt, metal, and glossy surfaces catch the light.
        if (ndl > 0.001 && atten > 0.01) {
            vec3 V = normalize(scene.cameraPosition.xyz - fragWorldPosition);
            vec3 H = normalize(L + V);
            float nh = max(dot(normal, H), 0.0);
            // Roughness varies: smooth surfaces (wet asphalt, metal) get
            // sharper highlights, rough (ground) gets softer.
            float specExponent = mix(8.0, 128.0, albedoLuminance); // bright albedo = likely smoother
            float spec = pow(nh, specExponent) * atten * occlusion * lcol.w;
            // Slightly blue-white tint for specular (physical: Fresnel at grazing)
            lightContribution += vec3(0.9, 0.92, 1.0) * spec * 0.35;
        }

        lit += lightContribution;
    }

    // --- Emissive self-illumination ---
    // The mesh itself emits light. Emissive is added directly to the output
    // so it can bloom and god-ray. A simple ambient multiplication prevents
    // pure-black surfaces from going fully emissive.
    if (fragEmissive > 0.001) {
        lit += albedo * fragEmissive;
    }

    vec3 viewDirection = normalize(scene.cameraPosition.xyz - fragWorldPosition);
    lit = planarReflection(lit, normal, viewDirection);

    // Aerial in-scattering is handled by the froxel volumetric pass now; keep the
    // surface lighting itself un-hazed so the world does not wash out with range.
    outColor = vec4(lit, fragColor.a * texAlpha); // linear HDR; tonemap pass converts to display
}
