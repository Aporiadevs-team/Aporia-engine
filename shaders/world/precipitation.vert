#version 450

// One instanced quad per precipitation particle. The quad is billboarded on the
// camera right/up axes and elongated along the particle's screen-space fall
// velocity, so rain becomes a wind-slanted streak while snow stays round.
layout(location = 0) in vec4 inPosition; // xyz world position, w kind (0 rain, 1 snow, 2 sleet)
layout(location = 1) in vec4 inVelocity; // xyz m/s, w half-width
layout(location = 2) in vec4 inSize;     // x half-length, y alpha, z phase, w unused

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraPosition; // xyz camera, w particle box radius
    vec4 fog;            // rgb fog colour, a fog density (1/m)
} push;

layout(location = 0) out vec2 outUv;
layout(location = 1) out float outKind;
layout(location = 2) out float outAlpha;

void main() {
    const vec2 corner = vec2(float(gl_VertexIndex & 1) * 2.0 - 1.0, float(gl_VertexIndex >> 1) * 2.0 - 1.0);
    const vec3 center = inPosition.xyz;

    const vec2 velocity = vec2(dot(inVelocity.xyz, push.cameraRight.xyz),
                               dot(inVelocity.xyz, push.cameraUp.xyz));
    const vec2 axis = length(velocity) > 1e-4 ? normalize(velocity) : vec2(0.0, -1.0);
    const vec2 side = vec2(-axis.y, axis.x);
    const vec3 offset =
        push.cameraRight.xyz * (corner.x * inVelocity.w * side.x + corner.y * inSize.x * axis.x) +
        push.cameraUp.xyz * (corner.x * inVelocity.w * side.y + corner.y * inSize.x * axis.y);

    const float distance = length(center - push.cameraPosition.xyz);
    const float edgeFade = 1.0 - smoothstep(push.cameraPosition.w * 0.65, push.cameraPosition.w, distance);
    const float fogAmount = 1.0 - exp(-distance * push.fog.a);

    outUv = corner;
    outKind = inPosition.w;
    outAlpha = inSize.y * edgeFade * (1.0 - 0.75 * fogAmount);
    gl_Position = push.viewProj * vec4(center + offset, 1.0);
}
