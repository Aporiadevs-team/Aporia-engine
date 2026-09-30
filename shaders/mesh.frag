#version 450

// Mesh lighting: sun diffuse + shadows + reflection blend, output linear HDR.
// Quality tiers arrive as specialization constants, so one pipeline serves
// Low/Medium/High: PCF taps and reflection toggle compile per preset.
layout(constant_id = 0) const int SHADOW_TAPS = 9;   // 1 Low, 9 Medium (3x3), 25 High (5x5)
layout(constant_id = 1) const float REFLECTIONS_ON = 1.0;

layout(set = 0, binding = 0) uniform sampler2D albedoTexture;
layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform sampler2D reflectionTexture; // mirror plane texture (screen space)

layout(set = 0, binding = 3) uniform SceneUniforms {
    mat4 sunViewProj;     // world -> sun clip space
    vec4 sunDirection;    // xyz toward light, w intensity
    vec4 cameraPosition;  // xyz world, w unused
    vec4 shadingParams;   // x ambient, y shadow strength, z mirror z, w shadow map texel size
    vec4 screenParams;    // x,y viewport size, z,y unused
} scene;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUv;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) in vec4 fragShadowPosition; // sun clip space
layout(location = 4) in vec4 fragClipPosition;   // main camera clip space

layout(location = 0) out vec4 outColor;

float shadowSample(vec2 uv, float depth, float compare) {
    return depth - 0.0015 < texture(shadowMap, uv).r ? 1.0 : 0.0;
}

float pcfShadow(vec3 normal) {
    if (SHADOW_TAPS <= 1) {
        return 1.0; // Low: plain lighting, no shadow lookup cost
    }
    if (fragShadowPosition.w <= 0.0) {
        return 1.0; // behind the sun camera: outside the map, full light
    }
    vec3 projected = fragShadowPosition.xyz / fragShadowPosition.w;
    if (any(lessThan(projected.xy, vec2(0.0))) || any(greaterThan(projected.xy, vec2(1.0)))) {
        return 1.0;
    }
    const float bias = max(0.0015 * (1.0 - dot(normal, scene.sunDirection.xyz)), 0.0005);
    const float depth = projected.z - bias;
    const float texel = scene.shadingParams.w;
    float lit = 0.0;
    const int radius = SHADOW_TAPS <= 9 ? 1 : 2;
    float total = 0.0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            lit += shadowSample(projected.xy + vec2(float(dx), float(dy)) * texel, depth, 0.0);
            total += 1.0;
        }
    }
    return mix(scene.shadingParams.y, 1.0, lit / total);
}

vec3 planarReflection(vec3 baseColor, vec3 normal, vec3 viewDirection) {
    // RenderItem does not identify mirror surfaces yet. Sampling the reflected
    // scene onto every fragment overlays an upside-down duplicate over the
    // world, so keep the ordinary scene stable until mirror materials exist.
    return baseColor;
}

void main() {
    vec3 albedo = texture(albedoTexture, fragUv).rgb;
    vec3 normal = normalize(fragNormal);

    float diffuse = max(dot(normal, scene.sunDirection.xyz), 0.0);
    float shadow = pcfShadow(normal);
    vec3 lit = albedo * (scene.shadingParams.x + diffuse * shadow * scene.sunDirection.w);

    vec3 viewDirection = normalize(scene.cameraPosition.xyz - fragWorldPosition);
    lit = planarReflection(lit, normal, viewDirection);

    outColor = vec4(lit, 1.0); // linear HDR; tonemap pass converts to display
}
