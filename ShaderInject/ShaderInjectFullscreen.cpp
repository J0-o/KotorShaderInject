#include <windows.h>
#include <GL/gl.h>

#include <cstdint>
#include <cstring>

#include "ShaderInjectFullscreenProvider.h"

#pragma comment(lib, "opengl32.lib")

namespace {

using GLchar = char;

constexpr GLenum GL_ACTIVE_TEXTURE_VALUE = 0x84E0;
constexpr GLenum GL_CLAMP_TO_EDGE_VALUE = 0x812F;
constexpr GLenum GL_COMPILE_STATUS_VALUE = 0x8B81;
constexpr GLenum GL_CURRENT_PROGRAM_VALUE = 0x8B8D;
constexpr GLenum GL_FRAGMENT_SHADER_VALUE = 0x8B30;
constexpr GLenum GL_LINK_STATUS_VALUE = 0x8B82;
constexpr GLenum GL_RGBA8_VALUE = 0x8058;
constexpr GLenum GL_TEXTURE0_VALUE = 0x84C0;
constexpr GLenum GL_VERTEX_SHADER_VALUE = 0x8B31;
constexpr unsigned int kMaxFullscreenEffects = 16;

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
using GlUniform1fFn = void (APIENTRY*)(GLint, GLfloat);
using GlActiveTextureFn = void (APIENTRY*)(GLenum);

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
GlUniform1fFn g_glUniform1f = nullptr;
GlActiveTextureFn g_glActiveTexture = nullptr;
bool g_functionsLoaded = false;

struct FullscreenEffectState {
    const ShaderInjectFullscreenEffectV1* descriptor;
    ShaderInjectFullscreenEffectV1 effect;
    HGLRC context;
    GLuint sceneTexture;
    GLuint program;
    int textureWidth;
    int textureHeight;
    std::uint32_t frameIndex;
};

FullscreenEffectState g_effects[kMaxFullscreenEffects]{};
unsigned int g_effectCount = 0;
SRWLOCK g_effectLock = SRWLOCK_INIT;

const char* kVertexShader = R"GLSL(
#version 120
varying vec2 vUv;
void main() {
    gl_Position = gl_Vertex;
    vUv = gl_MultiTexCoord0.xy;
}
)GLSL";

void Log(const char* message) {
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

bool IsValidGlPointer(void* pointer) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
    return pointer && value != 1 && value != 2 && value != 3 && value != 4 &&
           value != static_cast<uintptr_t>(-1);
}

template <typename T>
bool LoadFunction(T& function, const char* name) {
    function = reinterpret_cast<T>(wglGetProcAddress(name));
    if (!IsValidGlPointer(reinterpret_cast<void*>(function))) {
        HMODULE module = GetModuleHandleA("opengl32.dll");
        function = module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
    }
    return function != nullptr;
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
        LoadFunction(g_glUniform1f, "glUniform1f") &&
        LoadFunction(g_glActiveTexture, "glActiveTexture");
    return g_functionsLoaded;
}

GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = g_glCreateShader(type);
    if (!shader) return 0;
    g_glShaderSource(shader, 1, &source, nullptr);
    g_glCompileShader(shader);
    GLint compiled = GL_FALSE;
    g_glGetShaderiv(shader, GL_COMPILE_STATUS_VALUE, &compiled);
    if (compiled == GL_TRUE) return shader;
    char log[4096] = {};
    g_glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    Log(log[0] ? log : "[ShaderInject] Fullscreen shader compilation failed.");
    g_glDeleteShader(shader);
    return 0;
}

bool CreateProgram(FullscreenEffectState& state) {
    GLuint vertex = CompileShader(GL_VERTEX_SHADER_VALUE, kVertexShader);
    GLuint fragment = CompileShader(
        GL_FRAGMENT_SHADER_VALUE, state.effect.fragmentShaderSource);
    if (!vertex || !fragment) {
        if (vertex) g_glDeleteShader(vertex);
        if (fragment) g_glDeleteShader(fragment);
        return false;
    }
    state.program = g_glCreateProgram();
    g_glAttachShader(state.program, vertex);
    g_glAttachShader(state.program, fragment);
    g_glLinkProgram(state.program);
    g_glDeleteShader(vertex);
    g_glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    g_glGetProgramiv(state.program, GL_LINK_STATUS_VALUE, &linked);
    if (linked == GL_TRUE) return true;
    char log[4096] = {};
    g_glGetProgramInfoLog(state.program, sizeof(log), nullptr, log);
    Log(log[0] ? log : "[ShaderInject] Fullscreen program link failed.");
    g_glDeleteProgram(state.program);
    state.program = 0;
    return false;
}

bool EnsureResources(FullscreenEffectState& state, int width, int height) {
    HGLRC context = wglGetCurrentContext();
    if (!context || width <= 0 || height <= 0 || !LoadFunctions()) return false;
    if (context != state.context) {
        state.context = context;
        state.sceneTexture = 0;
        state.program = 0;
        state.textureWidth = 0;
        state.textureHeight = 0;
    }
    if (!state.program && !CreateProgram(state)) return false;
    if (state.sceneTexture && width == state.textureWidth &&
        height == state.textureHeight) {
        return true;
    }
    if (state.sceneTexture) glDeleteTextures(1, &state.sceneTexture);
    glGenTextures(1, &state.sceneTexture);
    glBindTexture(GL_TEXTURE_2D, state.sceneTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_VALUE, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    state.textureWidth = width;
    state.textureHeight = height;
    return state.sceneTexture != 0;
}

void DrawFullscreenQuad() {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
}

FullscreenEffectState* FindOrAddEffect(
    const ShaderInjectFullscreenEffectV1* effect) {
    for (unsigned int index = 0; index < g_effectCount; ++index) {
        auto& state = g_effects[index];
        if (state.descriptor == effect) return &state;
        if (std::strcmp(state.effect.effectId, effect->effectId) != 0) continue;
        const bool sameSource = std::strcmp(
            state.effect.fragmentShaderSource,
            effect->fragmentShaderSource) == 0;
        const bool sameFrameUniform =
            (!state.effect.frameIndexUniform && !effect->frameIndexUniform) ||
            (state.effect.frameIndexUniform && effect->frameIndexUniform &&
             std::strcmp(state.effect.frameIndexUniform,
                         effect->frameIndexUniform) == 0);
        return sameSource && sameFrameUniform ? &state : nullptr;
    }
    if (g_effectCount >= kMaxFullscreenEffects) return nullptr;
    auto& state = g_effects[g_effectCount++];
    state.descriptor = effect;
    state.effect = *effect;
    return &state;
}

bool ApplyEffect(FullscreenEffectState& state) {
    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0 || !LoadFunctions()) return false;

    GLint previousProgram = 0;
    GLint previousActiveTexture = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM_VALUE, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &previousActiveTexture);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    if (!EnsureResources(state, width, height)) {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        g_glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, state.sceneTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        viewport[0], viewport[1], width, height);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
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

    g_glUseProgram(state.program);
    g_glUniform1i(g_glGetUniformLocation(state.program, "sceneTexture"), 0);
    if (state.effect.frameIndexUniform && state.effect.frameIndexUniform[0]) {
        g_glUniform1f(
            g_glGetUniformLocation(state.program, state.effect.frameIndexUniform),
            static_cast<float>(state.frameIndex));
        state.frameIndex = (state.frameIndex + 1) & 4095u;
    }
    DrawFullscreenQuad();

    g_glUseProgram(static_cast<GLuint>(previousProgram));
    glPopAttrib();
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    g_glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    return true;
}

bool IsValidEffect(const ShaderInjectFullscreenEffectV1* effect) {
    return effect && effect->structSize >= sizeof(*effect) &&
           effect->abiVersion == SHADER_INJECT_FULLSCREEN_ABI_VERSION &&
           effect->effectId && effect->effectId[0] &&
           effect->fragmentShaderSource && effect->fragmentShaderSource[0];
}

} // namespace

extern "C" BOOL __cdecl ShaderInject_ApplyFullscreenEffect(
    const ShaderInjectFullscreenEffectV1* effect) {
    if (!IsValidEffect(effect)) return FALSE;
    AcquireSRWLockExclusive(&g_effectLock);
    FullscreenEffectState* state = FindOrAddEffect(effect);
    ReleaseSRWLockExclusive(&g_effectLock);
    return state && ApplyEffect(*state) ? TRUE : FALSE;
}
