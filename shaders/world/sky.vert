#version 450

// Fullscreen sky pass: a quad covering the target. The fragment stage rebuilds
// the world-space view ray, so the sky lives at infinity (no box to fly out of).
layout(location = 0) out vec2 fragUv;

void main() {
    vec2 positions[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vec2 position = positions[gl_VertexIndex];
    fragUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
