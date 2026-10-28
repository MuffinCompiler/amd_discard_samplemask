// AMD OpenGL driver bug repro: with GL_SAMPLE_ALPHA_TO_COVERAGE enabled, a
// fragment shader that contains "discard" AND writes gl_SampleMask produces no
// output at all in a multisample framebuffer -- even though the discard is
// never executed and alpha is 1.0.
//
// Run without arguments. Each case draws a red full-screen quad into a 4x MSAA
// framebuffer cleared to black, resolves it, prints the centre pixel plus the
// fragment shader invocations and passed samples of the draw.
//
// Build: cl /nologo /EHsc /O2 /MT amd_discard_samplemask.cpp opengl32.lib user32.lib gdi32.lib

#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

typedef char GLchar;
typedef unsigned long long GLuint64;
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_VERTEX_SHADER                0x8B31
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_FRAMEBUFFER                  0x8D40
#define GL_READ_FRAMEBUFFER             0x8CA8
#define GL_DRAW_FRAMEBUFFER             0x8CA9
#define GL_RENDERBUFFER                 0x8D41
#define GL_COLOR_ATTACHMENT0            0x8CE0
#define GL_FRAMEBUFFER_COMPLETE         0x8CD5
#define GL_RGBA8                        0x8058
#define GL_MULTISAMPLE                  0x809D
#define GL_SAMPLE_ALPHA_TO_COVERAGE     0x809E
#define GL_SAMPLES_PASSED               0x8914
#define GL_FRAGMENT_SHADER_INVOCATIONS  0x82F4
#define GL_QUERY_RESULT                 0x8866

// WGL_ARB_create_context
#define WGL_CONTEXT_MAJOR_VERSION_ARB   0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB   0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB    0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001
typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);

#define GLFUNCS(X) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void,   glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void,   glCompileShader, (GLuint)) \
    X(void,   glGetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void,   glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(GLuint, glCreateProgram, ()) \
    X(void,   glAttachShader, (GLuint, GLuint)) \
    X(void,   glLinkProgram, (GLuint)) \
    X(void,   glGetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void,   glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void,   glUseProgram, (GLuint)) \
    X(GLint,  glGetUniformLocation, (GLuint, const GLchar*)) \
    X(void,   glUniform1f, (GLint, GLfloat)) \
    X(void,   glGenFramebuffers, (GLsizei, GLuint*)) \
    X(void,   glBindFramebuffer, (GLenum, GLuint)) \
    X(void,   glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void,   glGenRenderbuffers, (GLsizei, GLuint*)) \
    X(void,   glBindRenderbuffer, (GLenum, GLuint)) \
    X(void,   glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void,   glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void,   glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void,   glGenVertexArrays, (GLsizei, GLuint*)) \
    X(void,   glBindVertexArray, (GLuint)) \
    X(void,   glGenQueries, (GLsizei, GLuint*)) \
    X(void,   glBeginQuery, (GLenum, GLuint)) \
    X(void,   glEndQuery, (GLenum)) \
    X(void,   glGetQueryObjectui64v, (GLuint, GLenum, GLuint64*))

#define DECLARE(ret, name, args) typedef ret (APIENTRY *PFN_##name) args; static PFN_##name p_##name;
GLFUNCS(DECLARE)

static const int W = 64, H = 64, SAMPLES = 4;

// Shader files (in the "shaders" folder next to the executable) and whether
// alpha-to-coverage is enabled while drawing.
struct Case { const char* name; const char* fragFile; bool alphaToCoverage; };
static const Case CASES[] = {
    { "1_samplemask_a2c",            "samplemask.frag",         true  },
    { "2_samplemask_discard_a2c",    "samplemask_discard.frag", true  },
    { "3_samplemask_discard_no_a2c", "samplemask_discard.frag", false },
};

static std::string readFile(const std::string& path)
{
    std::string s;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { printf("cannot read %s\n", path.c_str()); return s; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 1, &src, nullptr);
    p_glCompileShader(s);
    GLint ok = 0;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; p_glGetShaderInfoLog(s, sizeof(log), nullptr, log); printf("compile error:\n%s\n", log); }
    return s;
}

static bool createCoreContext(HDC& dc)
{
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "amd_discard_samplemask";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, W, H,
                              nullptr, nullptr, wc.hInstance, nullptr);
    dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);

    // A legacy context is needed to get wglCreateContextAttribsARB.
    HGLRC legacy = wglCreateContext(dc);
    if (!legacy || !wglMakeCurrent(dc, legacy)) return false;
    auto createAttribs = (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!createAttribs) return false;

    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 6,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0 };
    HGLRC core = createAttribs(dc, nullptr, attribs);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    return core && wglMakeCurrent(dc, core);
}

int main()
{
    HDC dc;
    if (!createCoreContext(dc)) { printf("could not create OpenGL 4.6 core context\n"); return 1; }

#define LOAD(ret, name, args) p_##name = (PFN_##name)wglGetProcAddress(#name); \
    if (!p_##name) { printf("missing %s\n", #name); return 1; }
    GLFUNCS(LOAD)

    printf("AMD OpenGL bug repro: discard + gl_SampleMask + alpha-to-coverage\n\n");
    printf("GPU:    %s\nDriver: %s\n\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));

    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    *strrchr(exeDir, '\\') = '\0';
    const std::string shaderDir = std::string(exeDir) + "\\shaders\\";

    // 4x MSAA framebuffer to draw into, single-sample framebuffer to resolve into.
    GLuint msaaRbo, msaaFbo, resolveRbo, resolveFbo, vao, queries[2];
    p_glGenRenderbuffers(1, &msaaRbo);
    p_glBindRenderbuffer(GL_RENDERBUFFER, msaaRbo);
    p_glRenderbufferStorageMultisample(GL_RENDERBUFFER, SAMPLES, GL_RGBA8, W, H);
    p_glGenFramebuffers(1, &msaaFbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaRbo);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("MSAA FBO incomplete\n"); return 1; }

    p_glGenRenderbuffers(1, &resolveRbo);
    p_glBindRenderbuffer(GL_RENDERBUFFER, resolveRbo);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    p_glGenFramebuffers(1, &resolveFbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolveRbo);

    p_glGenVertexArrays(1, &vao);
    p_glBindVertexArray(vao);
    p_glGenQueries(2, queries);

    GLuint vs = compile(GL_VERTEX_SHADER, readFile(shaderDir + "fullscreen.vert").c_str());

    printf("Expected per draw: %d fragment shader invocations, %d samples passed.\n\n", W * H, W * H * SAMPLES);

    for (const Case& c : CASES)
    {
        GLuint prog = p_glCreateProgram();
        p_glAttachShader(prog, vs);
        p_glAttachShader(prog, compile(GL_FRAGMENT_SHADER, readFile(shaderDir + c.fragFile).c_str()));
        p_glLinkProgram(prog);
        GLint ok = 0;
        p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[2048]; p_glGetProgramInfoLog(prog, sizeof(log), nullptr, log); printf("link error:\n%s\n", log); continue; }

        // Draw the red quad into the black MSAA framebuffer, counting
        // fragment shader invocations and samples that pass.
        p_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo);
        glViewport(0, 0, W, H);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_MULTISAMPLE);
        if (c.alphaToCoverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        else                   glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        p_glUseProgram(prog);
        p_glUniform1f(p_glGetUniformLocation(prog, "never"), 1.0f);
        p_glBeginQuery(GL_FRAGMENT_SHADER_INVOCATIONS, queries[0]);
        p_glBeginQuery(GL_SAMPLES_PASSED, queries[1]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        p_glEndQuery(GL_SAMPLES_PASSED);
        p_glEndQuery(GL_FRAGMENT_SHADER_INVOCATIONS);

        // Resolve and read back.
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo);
        p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
        p_glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo);
        std::vector<unsigned char> px(W * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

        GLuint64 invocations = 0, samplesPassed = 0;
        p_glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &invocations);
        p_glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &samplesPassed);

        const unsigned char* centre = &px[((H / 2) * W + W / 2) * 4];
        bool red = centre[0] > 200 && centre[1] < 50 && centre[2] < 50;
        printf("%-29s centre pixel = (%3d,%3d,%3d)  FS invocations = %5llu  samples passed = %5llu  -> %s\n",
               c.name, centre[0], centre[1], centre[2], invocations, samplesPassed,
               red ? "OK (red)" : "BUG (expected red)");
    }

    printf("\nExpected: all cases red (alpha is 1.0, the discard is never executed).\n");
    printf("\nShader of case 2 (shaders\\%s):\n\n%s\n", CASES[1].fragFile,
           readFile(shaderDir + CASES[1].fragFile).c_str());
    printf("Press Enter to exit...");
    getchar();
    return 0;
}
