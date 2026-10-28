#version 430
uniform float never; // set to 1.0
layout(location = 0) out vec4 colour;
void main() {
    gl_SampleMask[0] = -1;
    colour = vec4(1.0, 0.0, 0.0, 1.0);
}