# AMD OpenGL: shader output lost with `discard` + `gl_SampleMask` + alpha-to-coverage

**Tested on:** AMD Radeon RX 7900 GRE, driver 26.10.1.261008, Windows 11.

## Shaders

A full-screen quad is drawn red into a 4x MSAA framebuffer that was cleared to
black, with `GL_SAMPLE_ALPHA_TO_COVERAGE` enabled.

`shaders/samplemask.frag` renders red as expected:

```glsl
#version 430
uniform float never; // set to 1.0
layout(location = 0) out vec4 colour;
void main() {
    gl_SampleMask[0] = -1;
    colour = vec4(1.0, 0.0, 0.0, 1.0);
}
```

`shaders/samplemask_discard.frag` adds one line, a `discard` that is never
executed, and renders nothing at all (black, no GL error):

```glsl
#version 430
uniform float never; // set to 1.0
layout(location = 0) out vec4 colour;
void main() {
    gl_SampleMask[0] = -1;
    if (never < 0.0) discard;
    colour = vec4(1.0, 0.0, 0.0, 1.0);
}
```

With alpha-to-coverage disabled, the second shader renders red as well.
The order of the statements does not matter.

## Cases

| Case | Fragment shader | alpha-to-coverage | FS invocations | Samples passed | Result |
|---|---|---|---|---|---|
| 1 | `samplemask.frag` | on | 4160 | 16384 | red |
| 2 | `samplemask_discard.frag` | on | 4160 | **0** | **black (bug)** |
| 3 | `samplemask_discard.frag` | off | 4160 | 16384 | red |

All cases use `shaders/fullscreen.vert`. The counters come from
`GL_FRAGMENT_SHADER_INVOCATIONS` and `GL_SAMPLES_PASSED` queries around the
draw. In case 2 the fragment shader runs for every fragment, but not a single
sample reaches the framebuffer.

## Build

From an x64 Visual Studio developer prompt:

```
cl /nologo /EHsc /O2 /MT amd_discard_samplemask.cpp opengl32.lib user32.lib gdi32.lib
```

## Run

Start `amd_discard_samplemask.exe`. It prints the results of each case:

```
Expected per draw: 4096 fragment shader invocations, 16384 samples passed.

1_samplemask_a2c              centre pixel = (255,  0,  0)  FS invocations =  4160  samples passed = 16384  -> OK (red)
2_samplemask_discard_a2c      centre pixel = (  0,  0,  0)  FS invocations =  4160  samples passed =     0  -> BUG (expected red)
3_samplemask_discard_no_a2c   centre pixel = (255,  0,  0)  FS invocations =  4160  samples passed = 16384  -> OK (red)
```

Disclaimer: The cpp code for the minimal example in this repo has been generated with Claude.
