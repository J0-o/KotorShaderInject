#include <windows.h>
#include <GL/gl.h>

#include <cstddef>
#include <cstdio>

#include "../ShaderInject/ShaderInjectProvider.h"
#include "AreaTex.h"
#include "SearchTex.h"
#include "SMAAReference.generated.h"

#pragma comment(lib, "opengl32.lib")

namespace {

using GLchar = char;

constexpr GLenum GL_ACTIVE_TEXTURE_VALUE = 0x84E0;
constexpr GLenum GL_COLOR_ATTACHMENT0_VALUE = 0x8CE0;
constexpr GLenum GL_COMPILE_STATUS_VALUE = 0x8B81;
constexpr GLenum GL_CURRENT_PROGRAM_VALUE = 0x8B8D;
constexpr GLenum GL_FRAMEBUFFER_BINDING_VALUE = 0x8CA6;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_VALUE = 0x8CD5;
constexpr GLenum GL_FRAMEBUFFER_VALUE = 0x8D40;
constexpr GLenum GL_FRAGMENT_SHADER_VALUE = 0x8B30;
constexpr GLenum GL_LINK_STATUS_VALUE = 0x8B82;
constexpr GLenum GL_RGB8_VALUE = 0x8051;
constexpr GLenum GL_RGBA8_VALUE = 0x8058;
constexpr GLenum GL_TEXTURE0_VALUE = 0x84C0;
constexpr GLenum GL_TEXTURE1_VALUE = 0x84C1;
constexpr GLenum GL_TEXTURE2_VALUE = 0x84C2;
constexpr GLenum GL_VERTEX_SHADER_VALUE = 0x8B31;

using GlCreateShaderFn = GLuint (APIENTRY*)(GLenum);
using GlShaderSourceFn = void (APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using GlCompileShaderFn = void (APIENTRY*)(GLuint);
using GlGetShaderivFn = void (APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetShaderInfoLogFn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlDeleteShaderFn = void (APIENTRY*)(GLuint);
using GlCreateProgramFn = GLuint (APIENTRY*)();
using GlAttachShaderFn = void (APIENTRY*)(GLuint, GLuint);
using GlLinkProgramFn = void (APIENTRY*)(GLuint);
using GlGetProgramivFn = void (APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetProgramInfoLogFn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlDeleteProgramFn = void (APIENTRY*)(GLuint);
using GlUseProgramFn = void (APIENTRY*)(GLuint);
using GlGetUniformLocationFn = GLint (APIENTRY*)(GLuint, const GLchar*);
using GlUniform1iFn = void (APIENTRY*)(GLint, GLint);
using GlUniform4fFn = void (APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using GlActiveTextureFn = void (APIENTRY*)(GLenum);
using GlGenFramebuffersFn = void (APIENTRY*)(GLsizei, GLuint*);
using GlBindFramebufferFn = void (APIENTRY*)(GLenum, GLuint);
using GlFramebufferTexture2DFn = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlCheckFramebufferStatusFn = GLenum (APIENTRY*)(GLenum);
using GlDeleteFramebuffersFn = void (APIENTRY*)(GLsizei, const GLuint*);

GlCreateShaderFn g_glCreateShader = nullptr;
GlShaderSourceFn g_glShaderSource = nullptr;
GlCompileShaderFn g_glCompileShader = nullptr;
GlGetShaderivFn g_glGetShaderiv = nullptr;
GlGetShaderInfoLogFn g_glGetShaderInfoLog = nullptr;
GlDeleteShaderFn g_glDeleteShader = nullptr;
GlCreateProgramFn g_glCreateProgram = nullptr;
GlAttachShaderFn g_glAttachShader = nullptr;
GlLinkProgramFn g_glLinkProgram = nullptr;
GlGetProgramivFn g_glGetProgramiv = nullptr;
GlGetProgramInfoLogFn g_glGetProgramInfoLog = nullptr;
GlDeleteProgramFn g_glDeleteProgram = nullptr;
GlUseProgramFn g_glUseProgram = nullptr;
GlGetUniformLocationFn g_glGetUniformLocation = nullptr;
GlUniform1iFn g_glUniform1i = nullptr;
GlUniform4fFn g_glUniform4f = nullptr;
GlActiveTextureFn g_glActiveTexture = nullptr;
GlGenFramebuffersFn g_glGenFramebuffers = nullptr;
GlBindFramebufferFn g_glBindFramebuffer = nullptr;
GlFramebufferTexture2DFn g_glFramebufferTexture2D = nullptr;
GlCheckFramebufferStatusFn g_glCheckFramebufferStatus = nullptr;
GlDeleteFramebuffersFn g_glDeleteFramebuffers = nullptr;

HGLRC g_resourceContext = nullptr;
GLuint g_sceneTexture = 0;
GLuint g_edgesTexture = 0;
GLuint g_blendTexture = 0;
GLuint g_areaTexture = 0;
GLuint g_searchTexture = 0;
GLuint g_framebuffer = 0;
GLuint g_edgeProgram = 0;
GLuint g_blendProgram = 0;
GLuint g_neighborhoodProgram = 0;
int g_textureWidth = 0;
int g_textureHeight = 0;
bool g_functionsLoaded = false;
bool g_enabled = true;
bool g_failureLogged = false;
bool g_firstFrameLogged = false;
const char* g_stage = "idle";

const char* kVertexShader = R"GLSL(
#version 120
varying vec2 vUv;
void main() {
    gl_Position = gl_Vertex;
    vUv = gl_MultiTexCoord0.xy;
}
)GLSL";

const char* kSmaaPreamble = R"GLSL(
#version 120
#define SMAA_CUSTOM_SL 1
#define SMAA_PRESET_HIGH 1
#define SMAA_RT_METRICS uMetrics
#define SMAA_AREATEX_SELECT(sample) (sample).ra
#define SMAA_SEARCHTEX_SELECT(sample) (sample).r
#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) texture2D(tex, coord)
#define SMAASampleLevelZeroPoint(tex, coord) texture2D(tex, coord)
#define SMAASampleLevelZeroOffset(tex, coord, offset) texture2D(tex, coord + vec2(offset) * uMetrics.xy)
#define SMAASample(tex, coord) texture2D(tex, coord)
#define SMAASamplePoint(tex, coord) texture2D(tex, coord)
#define SMAASampleOffset(tex, coord, offset) texture2D(tex, coord + vec2(offset) * uMetrics.xy)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define lerp(a, b, t) mix(a, b, t)
#define saturate(a) clamp(a, 0.0, 1.0)
#define mad(a, b, c) ((a) * (b) + (c))
#define round(a) floor((a) + 0.5)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4
uniform vec4 uMetrics;
)GLSL";

const char* kEdgeWrapper = R"GLSL(
uniform sampler2D colorTex;
varying vec2 vUv;
void main() {
    vec4 offset[3];
    SMAAEdgeDetectionVS(vUv, offset);
    gl_FragColor = vec4(SMAALumaEdgeDetectionPS(vUv, offset, colorTex), 0.0, 0.0);
}
)GLSL";

const char* kBlendWrapper = R"GLSL(
uniform sampler2D edgesTex;
uniform sampler2D areaTex;
uniform sampler2D searchTex;
varying vec2 vUv;
void main() {
    vec2 pixcoord;
    vec4 offset[3];
    SMAABlendingWeightCalculationVS(vUv, pixcoord, offset);
    gl_FragColor = SMAABlendingWeightCalculationPS(
        vUv, pixcoord, offset, edgesTex, areaTex, searchTex, vec4(0.0));
}
)GLSL";

const char* kNeighborhoodWrapper = R"GLSL(
uniform sampler2D colorTex;
uniform sampler2D blendTex;
varying vec2 vUv;
void main() {
    vec4 offset;
    SMAANeighborhoodBlendingVS(vUv, offset);
    gl_FragColor = SMAANeighborhoodBlendingPS(vUv, offset, colorTex, blendTex);
}
)GLSL";

void Log(const char* message) {
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

bool ValidExtensionPointer(PROC pointer) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
    return pointer && value != 1 && value != 2 && value != 3 &&
           value != static_cast<uintptr_t>(-1);
}

template <typename T>
bool LoadFunction(T& destination, const char* name) {
    PROC pointer = wglGetProcAddress(name);
    if (!ValidExtensionPointer(pointer)) return false;
    destination = reinterpret_cast<T>(pointer);
    return true;
}

template <typename T>
bool LoadFunction(T& destination, const char* coreName, const char* extensionName) {
    return LoadFunction(destination, coreName) ||
           LoadFunction(destination, extensionName);
}

bool LoadFunctions() {
    if (g_functionsLoaded) return true;
    g_functionsLoaded =
        LoadFunction(g_glCreateShader, "glCreateShader") &&
        LoadFunction(g_glShaderSource, "glShaderSource") &&
        LoadFunction(g_glCompileShader, "glCompileShader") &&
        LoadFunction(g_glGetShaderiv, "glGetShaderiv") &&
        LoadFunction(g_glGetShaderInfoLog, "glGetShaderInfoLog") &&
        LoadFunction(g_glDeleteShader, "glDeleteShader") &&
        LoadFunction(g_glCreateProgram, "glCreateProgram") &&
        LoadFunction(g_glAttachShader, "glAttachShader") &&
        LoadFunction(g_glLinkProgram, "glLinkProgram") &&
        LoadFunction(g_glGetProgramiv, "glGetProgramiv") &&
        LoadFunction(g_glGetProgramInfoLog, "glGetProgramInfoLog") &&
        LoadFunction(g_glDeleteProgram, "glDeleteProgram") &&
        LoadFunction(g_glUseProgram, "glUseProgram") &&
        LoadFunction(g_glGetUniformLocation, "glGetUniformLocation") &&
        LoadFunction(g_glUniform1i, "glUniform1i") &&
        LoadFunction(g_glUniform4f, "glUniform4f") &&
        LoadFunction(g_glActiveTexture, "glActiveTexture") &&
        LoadFunction(g_glGenFramebuffers, "glGenFramebuffers", "glGenFramebuffersEXT") &&
        LoadFunction(g_glBindFramebuffer, "glBindFramebuffer", "glBindFramebufferEXT") &&
        LoadFunction(g_glFramebufferTexture2D, "glFramebufferTexture2D", "glFramebufferTexture2DEXT") &&
        LoadFunction(g_glCheckFramebufferStatus, "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT") &&
        LoadFunction(g_glDeleteFramebuffers, "glDeleteFramebuffers", "glDeleteFramebuffersEXT");
    return g_functionsLoaded;
}

GLuint CompileShader(GLenum type, const char* const* sources, int sourceCount) {
    GLuint shader = g_glCreateShader(type);
    if (!shader) return 0;
    g_glShaderSource(shader, sourceCount, sources, nullptr);
    g_glCompileShader(shader);
    GLint compiled = GL_FALSE;
    g_glGetShaderiv(shader, GL_COMPILE_STATUS_VALUE, &compiled);
    if (compiled == GL_TRUE) return shader;

    char log[2048] = {};
    g_glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    Log(log[0] ? log : "[SMAA] Shader compilation failed.");
    g_glDeleteShader(shader);
    return 0;
}

bool CreateProgram(const char* wrapper, GLuint& program) {
    const char* vertexSources[] = {kVertexShader};
    const char* fragmentSources[] = {kSmaaPreamble, kSmaaReferenceSource, wrapper};
    GLuint vertex = CompileShader(GL_VERTEX_SHADER_VALUE, vertexSources, 1);
    GLuint fragment = CompileShader(GL_FRAGMENT_SHADER_VALUE, fragmentSources, 3);
    if (!vertex || !fragment) {
        if (vertex) g_glDeleteShader(vertex);
        if (fragment) g_glDeleteShader(fragment);
        return false;
    }

    program = g_glCreateProgram();
    g_glAttachShader(program, vertex);
    g_glAttachShader(program, fragment);
    g_glLinkProgram(program);
    g_glDeleteShader(vertex);
    g_glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    g_glGetProgramiv(program, GL_LINK_STATUS_VALUE, &linked);
    if (linked == GL_TRUE) return true;

    char log[2048] = {};
    g_glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    Log(log[0] ? log : "[SMAA] Program link failed.");
    g_glDeleteProgram(program);
    program = 0;
    return false;
}

void ConfigureTexture(GLenum minFilter, GLenum magFilter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
}

bool CreateLookupTextures() {
    glGenTextures(1, &g_areaTexture);
    glBindTexture(GL_TEXTURE_2D, g_areaTexture);
    ConfigureTexture(GL_LINEAR, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                 AREATEX_WIDTH, AREATEX_HEIGHT, 0,
                 GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, areaTexBytes);

    glGenTextures(1, &g_searchTexture);
    glBindTexture(GL_TEXTURE_2D, g_searchTexture);
    ConfigureTexture(GL_LINEAR, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                 SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, searchTexBytes);
    return g_areaTexture != 0 && g_searchTexture != 0;
}

bool EnsureResources(int width, int height) {
    HGLRC context = wglGetCurrentContext();
    if (!context || !LoadFunctions()) return false;
    if (context != g_resourceContext) {
        g_resourceContext = context;
        g_sceneTexture = g_edgesTexture = g_blendTexture = 0;
        g_areaTexture = g_searchTexture = g_framebuffer = 0;
        g_edgeProgram = g_blendProgram = g_neighborhoodProgram = 0;
        g_textureWidth = g_textureHeight = 0;
    }
    if (!g_edgeProgram && !CreateProgram(kEdgeWrapper, g_edgeProgram)) return false;
    if (!g_blendProgram && !CreateProgram(kBlendWrapper, g_blendProgram)) return false;
    if (!g_neighborhoodProgram &&
        !CreateProgram(kNeighborhoodWrapper, g_neighborhoodProgram)) return false;
    if (!g_areaTexture && !CreateLookupTextures()) return false;
    if (g_sceneTexture && g_edgesTexture && g_blendTexture && g_framebuffer &&
        width == g_textureWidth && height == g_textureHeight) return true;

    if (g_sceneTexture) glDeleteTextures(1, &g_sceneTexture);
    if (g_edgesTexture) glDeleteTextures(1, &g_edgesTexture);
    if (g_blendTexture) glDeleteTextures(1, &g_blendTexture);
    if (g_framebuffer) g_glDeleteFramebuffers(1, &g_framebuffer);
    g_sceneTexture = g_edgesTexture = g_blendTexture = g_framebuffer = 0;

    glGenTextures(1, &g_sceneTexture);
    glBindTexture(GL_TEXTURE_2D, g_sceneTexture);
    ConfigureTexture(GL_LINEAR, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8_VALUE, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &g_edgesTexture);
    glBindTexture(GL_TEXTURE_2D, g_edgesTexture);
    ConfigureTexture(GL_LINEAR, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_VALUE, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &g_blendTexture);
    glBindTexture(GL_TEXTURE_2D, g_blendTexture);
    ConfigureTexture(GL_LINEAR, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_VALUE, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_VALUE, &previousFramebuffer);
    g_glGenFramebuffers(1, &g_framebuffer);
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_framebuffer);
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_edgesTexture, 0);
    const bool complete = g_glCheckFramebufferStatus(GL_FRAMEBUFFER_VALUE) ==
                          GL_FRAMEBUFFER_COMPLETE_VALUE;
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(previousFramebuffer));
    if (!complete) return false;

    g_textureWidth = width;
    g_textureHeight = height;
    return true;
}

void SetMetrics(GLuint program, int width, int height) {
    g_glUniform4f(g_glGetUniformLocation(program, "uMetrics"),
                  1.0f / width, 1.0f / height,
                  static_cast<float>(width), static_cast<float>(height));
}

void DrawFullscreenQuad() {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
}

struct SavedGlState {
    GLint program = 0;
    GLint framebuffer = 0;
    GLint activeTexture = GL_TEXTURE0_VALUE;
    GLint textures[3] = {};
    bool captured = false;
    bool attribPushed = false;
};

void RestoreGlState(SavedGlState& state) {
    if (!state.captured) return;
    g_glUseProgram(static_cast<GLuint>(state.program));
    if (state.attribPushed) glPopAttrib();
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(state.framebuffer));
    for (int unit = 0; unit < 3; ++unit) {
        g_glActiveTexture(GL_TEXTURE0_VALUE + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures[unit]));
    }
    g_glActiveTexture(static_cast<GLenum>(state.activeTexture));
    state.captured = false;
    state.attribPushed = false;
}

bool DrawSmaa(SavedGlState& state) {
    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int x = viewport[0];
    const int y = viewport[1];
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0 ||
        !wglGetCurrentContext() || !LoadFunctions()) return false;

    glGetIntegerv(GL_CURRENT_PROGRAM_VALUE, &state.program);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_VALUE, &state.framebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &state.activeTexture);
    for (int unit = 0; unit < 3; ++unit) {
        g_glActiveTexture(GL_TEXTURE0_VALUE + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[unit]);
    }
    state.captured = true;
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    if (!EnsureResources(width, height)) {
        RestoreGlState(state);
        return false;
    }

    g_stage = "copy final scene";
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_sceneTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, width, height);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    state.attribPushed = true;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    g_stage = "edge detection";
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_framebuffer);
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_edgesTexture, 0);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
    g_glUseProgram(g_edgeProgram);
    SetMetrics(g_edgeProgram, width, height);
    g_glUniform1i(g_glGetUniformLocation(g_edgeProgram, "colorTex"), 0);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_sceneTexture);
    DrawFullscreenQuad();

    g_stage = "blending weights";
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_blendTexture, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    g_glUseProgram(g_blendProgram);
    SetMetrics(g_blendProgram, width, height);
    g_glUniform1i(g_glGetUniformLocation(g_blendProgram, "edgesTex"), 0);
    g_glUniform1i(g_glGetUniformLocation(g_blendProgram, "areaTex"), 1);
    g_glUniform1i(g_glGetUniformLocation(g_blendProgram, "searchTex"), 2);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_edgesTexture);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_areaTexture);
    g_glActiveTexture(GL_TEXTURE2_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_searchTexture);
    DrawFullscreenQuad();

    g_stage = "neighborhood blending";
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(state.framebuffer));
    glViewport(x, y, width, height);
    g_glUseProgram(g_neighborhoodProgram);
    SetMetrics(g_neighborhoodProgram, width, height);
    g_glUniform1i(g_glGetUniformLocation(g_neighborhoodProgram, "colorTex"), 0);
    g_glUniform1i(g_glGetUniformLocation(g_neighborhoodProgram, "blendTex"), 1);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_sceneTexture);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_blendTexture);
    DrawFullscreenQuad();

    g_stage = "restore OpenGL state";
    RestoreGlState(state);
    g_stage = "idle";
    return true;
}

int HandleException(EXCEPTION_POINTERS* exception) {
    char message[256] = {};
    sprintf_s(message, "[SMAA] Exception 0x%08lX at %p during '%s'.",
              exception->ExceptionRecord->ExceptionCode,
              exception->ExceptionRecord->ExceptionAddress,
              g_stage);
    Log(message);
    return EXCEPTION_EXECUTE_HANDLER;
}

void __cdecl OnFinalScene(const ShaderInjectFrameContext* frame) {
    if (!g_enabled || !ShaderInject_IsMainPerspectiveCamera(frame)) return;
    SavedGlState savedState;
    __try {
        if (!DrawSmaa(savedState)) {
            if (!g_failureLogged) {
                g_failureLogged = true;
                Log("[SMAA] OpenGL shader or framebuffer resources are unavailable.");
            }
            return;
        }
        if (!g_firstFrameLogged) {
            g_firstFrameLogged = true;
            Log("[SMAA] First final-scene SMAA 1x composite completed.");
        }
    }
    __except (HandleException(GetExceptionInformation())) {
        RestoreGlState(savedState);
        g_enabled = false;
        Log("[SMAA] Disabled after an exception.");
    }
}

} // namespace

extern "C" BOOL __cdecl KPatch_Initialize() {
    static LONG initialized = 0;
    if (InterlockedCompareExchange(&initialized, 1, 0) != 0) return TRUE;

    HMODULE shaderInject = GetModuleHandleA("shader-inject.dll");
    if (!shaderInject) {
        InterlockedExchange(&initialized, 0);
        return FALSE;
    }
    auto registerProvider = reinterpret_cast<ShaderInjectRegisterProviderFn>(
        GetProcAddress(shaderInject, "ShaderInject_RegisterProvider"));
    const ShaderInjectProviderV1 provider = {
        sizeof(ShaderInjectProviderV1),
        SHADER_INJECT_ABI_VERSION,
        nullptr,
        nullptr,
        &OnFinalScene,
        nullptr,
        nullptr,
        SHADER_INJECT_PRIORITY_ANTI_ALIASING,
    };
    if (!registerProvider || !registerProvider(&provider)) {
        InterlockedExchange(&initialized, 0);
        return FALSE;
    }
    Log("[SMAA] ShaderInject post-DOF SMAA 1x provider loaded.");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        return KPatch_Initialize();
    }
    return TRUE;
}
