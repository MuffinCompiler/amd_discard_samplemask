#version 430
// Full-screen quad from gl_VertexID, no vertex buffers needed.
void main() {
    vec2 p = vec2((gl_VertexID & 1) * 2 - 1, (gl_VertexID >> 1) * 2 - 1);
    gl_Position = vec4(p, 0.0, 1.0);
}