#version 450

// Fullscreen pass: renders a triangle covering the target.
layout(location = 0) out vec2 fragUv;

void main() {
    // Triangle strip covering the viewport; UV in [0,1], origin top-left.
    vec2 positions[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vec2 position = positions[gl_VertexIndex];
    fragUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
