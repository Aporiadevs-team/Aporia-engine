#version 450

// Temporal anti-aliasing resolve. Reprojects the previous resolved frame with
// the previous view-projection and the scene depth, clamps it to the current
// neighborhood to fight ghosting, then blends. The scene is rendered with a
// sub-pixel jitter, so the accumulated history resolves edges.
layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D historyColor;
layout(set = 0, binding = 2) uniform sampler2D depthTexture;
layout(set = 0, binding = 3) uniform TaaUniforms {
    mat4 prevViewProj;
    mat4 invViewProj;
    vec4 params;     // x = blend strength, y = history valid (0/1), zw = texel size
    vec4 resolution; // xy = target size
} u;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 current = texture(currentColor, fragUv).rgb;
    float depth = texture(depthTexture, fragUv).r;

    vec3 resolved = current;
    if (u.params.y > 0.5 && depth < 1.0) {
        // Reconstruct the world point under this pixel and project it with the
        // previous frame's camera to find where it was.
        vec4 clip = vec4(fragUv * 2.0 - 1.0, depth, 1.0);
        vec4 world = u.invViewProj * clip;
        world /= world.w;
        vec4 previous = u.prevViewProj * world;
        vec2 previousUv = previous.xy / previous.w * 0.5 + 0.5;

        if (previous.w > 0.0 && all(greaterThanEqual(previousUv, vec2(0.0))) &&
            all(lessThanEqual(previousUv, vec2(1.0)))) {
            vec3 history = texture(historyColor, previousUv).rgb;

            // Neighborhood clamp: keep the history inside the current local
            // color range so moving edges do not leave trails.
            vec3 minimum = current;
            vec3 maximum = current;
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    vec3 tap = texture(currentColor, fragUv + vec2(float(x), float(y)) * u.params.zw).rgb;
                    minimum = min(minimum, tap);
                    maximum = max(maximum, tap);
                }
            }
            history = clamp(history, minimum, maximum);
            resolved = mix(current, history, clamp(u.params.x, 0.0, 0.98));
        }
    }
    outColor = vec4(resolved, 1.0);
}
