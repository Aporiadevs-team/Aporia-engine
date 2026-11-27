#version 450

// Premultiplied-alpha particle sprites: a needle for rain streaks, a soft disc
// for snow, and their average for sleet.
layout(location = 0) in vec2 inUv;
layout(location = 1) in float inKind;
layout(location = 2) in float inAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    const float across = abs(inUv.x);
    const float along = abs(inUv.y);
    const float streak = (1.0 - across * across) * (1.0 - along * along * along);
    const float flake = max(0.0, 1.0 - dot(inUv, inUv));
    const float snow = flake * flake;

    float shape;
    if (inKind < 0.5) {
        shape = streak;
    } else if (inKind < 1.5) {
        shape = snow;
    } else {
        shape = 0.5 * (streak + snow);
    }

    const float alpha = clamp(shape * inAlpha, 0.0, 1.0);
    if (alpha < 0.002) {
        discard;
    }
    const vec3 rainColor = vec3(0.62, 0.70, 0.82);
    const vec3 snowColor = vec3(0.96, 0.98, 1.00);
    const vec3 color = inKind < 0.5 ? rainColor
                     : inKind < 1.5 ? snowColor
                     : mix(rainColor, snowColor, 0.5);
    outColor = vec4(color * alpha, alpha);
}
