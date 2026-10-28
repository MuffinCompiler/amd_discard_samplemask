#version 430
uniform float never; // set to 1.0, so the discard is never executed
layout(location = 0) out vec4 colour;
void main() {
    gl_SampleMask[0] = -1;
    if (never < 0.0) discard;
    colour = vec4(1.0, 0.0, 0.0, 1.0);
}