#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "DepthOfFieldEffect.h"

#pragma comment(lib, "opengl32.lib")

namespace {

using GLchar = char;

constexpr float kFocusResponseSeconds = 0.35f;

constexpr GLenum GL_ACTIVE_TEXTURE_VALUE = 0x84E0;
constexpr GLenum GL_COLOR_ATTACHMENT0_VALUE = 0x8CE0;
constexpr GLenum GL_COMPILE_STATUS_VALUE = 0x8B81;
constexpr GLenum GL_CURRENT_PROGRAM_VALUE = 0x8B8D;
constexpr GLenum GL_DEPTH_COMPONENT24_VALUE = 0x81A6;
constexpr GLenum GL_FRAMEBUFFER_BINDING_VALUE = 0x8CA6;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_VALUE = 0x8CD5;
constexpr GLenum GL_FRAMEBUFFER_VALUE = 0x8D40;
constexpr GLenum GL_FRAGMENT_SHADER_VALUE = 0x8B30;
constexpr GLenum GL_LINK_STATUS_VALUE = 0x8B82;
constexpr GLenum GL_RGB8_VALUE = 0x8051;
constexpr GLenum GL_RGBA8_VALUE = 0x8058;
constexpr GLenum GL_TEXTURE0_VALUE = 0x84C0;
constexpr GLenum GL_TEXTURE1_VALUE = 0x84C1;
constexpr GLenum GL_TEXTURE3_VALUE = 0x84C3;
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
using GlUniform1fFn = void (APIENTRY*)(GLint, GLfloat);
using GlUniform2fFn = void (APIENTRY*)(GLint, GLfloat, GLfloat);
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
GlUniform1fFn g_glUniform1f = nullptr;
GlUniform2fFn g_glUniform2f = nullptr;
GlActiveTextureFn g_glActiveTexture = nullptr;
GlGenFramebuffersFn g_glGenFramebuffers = nullptr;
GlBindFramebufferFn g_glBindFramebuffer = nullptr;
GlFramebufferTexture2DFn g_glFramebufferTexture2D = nullptr;
GlCheckFramebufferStatusFn g_glCheckFramebufferStatus = nullptr;
GlDeleteFramebuffersFn g_glDeleteFramebuffers = nullptr;

HGLRC g_resourceContext = nullptr;
GLuint g_colorTexture = 0;
GLuint g_depthTexture = 0;
GLuint g_nearTexture = 0;
GLuint g_farTexture = 0;
GLuint g_framebuffer = 0;
GLuint g_blurProgram = 0;
GLuint g_compositeProgram = 0;
int g_textureWidth = 0;
int g_textureHeight = 0;
bool g_functionsLoaded = false;
bool g_failureLogged = false;
bool g_firstFrameLogged = false;
bool g_focusValid = false;
float g_focusDistance = 0.0f;
DWORD g_lastFocusTick = 0;
const char* g_stage = "idle";

const char* kVertexShader = R"GLSL(
#version 120
varying vec2 vUv;
void main() {
    gl_Position = gl_Vertex;
    vUv = gl_MultiTexCoord0.xy;
}
)GLSL";

const char* kBlurShader = R"GLSL(
#version 120
uniform sampler2D colorTexture;
uniform sampler2D depthTexture;
uniform vec2 inverseResolution;
uniform float nearPlane;
uniform float farPlane;
uniform float focusDistance;
uniform float sampleRadius;
uniform int horizontalPass;
varying vec2 vUv;

float viewDepth(vec2 uv) {
    float d = texture2D(depthTexture, uv).r;
    float ndc = d * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
           (farPlane + nearPlane - ndc * (farPlane - nearPlane));
}

vec4 weightedSample(vec2 uv) {
    return texture2D(colorTexture, uv);
}

void main() {
    vec2 axis = horizontalPass != 0
        ? vec2(inverseResolution.x, 0.0)
        : vec2(0.0, inverseResolution.y);
    float centerZ = viewDepth(vUv);
    float centerCoc = clamp(((centerZ - focusDistance) / max(centerZ, 0.001)) * 2.0,
                            -1.0, 1.0);
    float radiusScale = smoothstep(0.0, 1.0, centerCoc);
    vec2 stepUv = axis * (sampleRadius * radiusScale * 0.25);
    vec4 sum = weightedSample(vUv) * 0.2270270270;
    sum += weightedSample(clamp(vUv + stepUv, vec2(0.001), vec2(0.999))) * 0.1945945946;
    sum += weightedSample(clamp(vUv - stepUv, vec2(0.001), vec2(0.999))) * 0.1945945946;
    sum += weightedSample(clamp(vUv + stepUv * 2.0, vec2(0.001), vec2(0.999))) * 0.1216216216;
    sum += weightedSample(clamp(vUv - stepUv * 2.0, vec2(0.001), vec2(0.999))) * 0.1216216216;
    sum += weightedSample(clamp(vUv + stepUv * 3.0, vec2(0.001), vec2(0.999))) * 0.0540540541;
    sum += weightedSample(clamp(vUv - stepUv * 3.0, vec2(0.001), vec2(0.999))) * 0.0540540541;
    sum += weightedSample(clamp(vUv + stepUv * 4.0, vec2(0.001), vec2(0.999))) * 0.0162162162;
    sum += weightedSample(clamp(vUv - stepUv * 4.0, vec2(0.001), vec2(0.999))) * 0.0162162162;
    gl_FragColor = sum;
}
)GLSL";

const char* kCompositeShader = R"GLSL(
#version 120
uniform sampler2D colorTexture;
uniform sampler2D depthTexture;
uniform sampler2D farTexture;
uniform float nearPlane;
uniform float farPlane;
uniform float focusDistance;
varying vec2 vUv;

float viewDepth(vec2 uv) {
    float d = texture2D(depthTexture, uv).r;
    float ndc = d * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
           (farPlane + nearPlane - ndc * (farPlane - nearPlane));
}

float farCoverage() {
    float z = viewDepth(vUv);
    float coc = clamp(((z - focusDistance) / max(z, 0.001)) * 2.0,
                      -1.0, 1.0);
    return smoothstep(0.0, 1.0, coc);
}

void main() {
    vec3 sharp = texture2D(colorTexture, vUv).rgb;
    vec4 farBlur = texture2D(farTexture, vUv);
    float farAmount = farCoverage();
    vec3 color = mix(sharp, farBlur.rgb, farAmount);
    gl_FragColor = vec4(color, 1.0);
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
        LoadFunction(g_glUniform1f, "glUniform1f") &&
        LoadFunction(g_glUniform2f, "glUniform2f") &&
        LoadFunction(g_glActiveTexture, "glActiveTexture") &&
        LoadFunction(g_glGenFramebuffers, "glGenFramebuffers", "glGenFramebuffersEXT") &&
        LoadFunction(g_glBindFramebuffer, "glBindFramebuffer", "glBindFramebufferEXT") &&
        LoadFunction(g_glFramebufferTexture2D, "glFramebufferTexture2D", "glFramebufferTexture2DEXT") &&
        LoadFunction(g_glCheckFramebufferStatus, "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT") &&
        LoadFunction(g_glDeleteFramebuffers, "glDeleteFramebuffers", "glDeleteFramebuffersEXT");
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
    char log[2048] = {};
    g_glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    Log(log[0] ? log : "[DOF] Shader compilation failed.");
    g_glDeleteShader(shader);
    return 0;
}

bool CreateProgram(const char* fragmentSource, GLuint& program) {
    GLuint vertex = CompileShader(GL_VERTEX_SHADER_VALUE, kVertexShader);
    GLuint fragment = CompileShader(GL_FRAGMENT_SHADER_VALUE, fragmentSource);
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
    Log(log[0] ? log : "[DOF] Program link failed.");
    g_glDeleteProgram(program);
    program = 0;
    return false;
}

void ConfigureTexture(GLenum filter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
}

bool EnsureResources(int width, int height) {
    HGLRC context = wglGetCurrentContext();
    if (!context || !LoadFunctions()) return false;
    if (context != g_resourceContext) {
        g_resourceContext = context;
        g_colorTexture = g_depthTexture = 0;
        g_nearTexture = g_farTexture = g_framebuffer = 0;
        g_blurProgram = g_compositeProgram = 0;
        g_textureWidth = g_textureHeight = 0;
    }
    if (!g_blurProgram && !CreateProgram(kBlurShader, g_blurProgram)) return false;
    if (!g_compositeProgram &&
        !CreateProgram(kCompositeShader, g_compositeProgram)) return false;
    if (g_colorTexture && g_depthTexture && g_nearTexture && g_farTexture &&
        g_framebuffer && width == g_textureWidth && height == g_textureHeight) {
        return true;
    }
    if (g_colorTexture) glDeleteTextures(1, &g_colorTexture);
    if (g_depthTexture) glDeleteTextures(1, &g_depthTexture);
    if (g_nearTexture) glDeleteTextures(1, &g_nearTexture);
    if (g_farTexture) glDeleteTextures(1, &g_farTexture);
    if (g_framebuffer) g_glDeleteFramebuffers(1, &g_framebuffer);
    g_colorTexture = g_depthTexture = g_nearTexture = g_farTexture = g_framebuffer = 0;

    glGenTextures(1, &g_colorTexture);
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    ConfigureTexture(GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8_VALUE, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &g_depthTexture);
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
    ConfigureTexture(GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24_VALUE, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

    const int halfWidth = (width + 1) / 2;
    const int halfHeight = (height + 1) / 2;
    glGenTextures(1, &g_nearTexture);
    glBindTexture(GL_TEXTURE_2D, g_nearTexture);
    ConfigureTexture(GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_VALUE, halfWidth, halfHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenTextures(1, &g_farTexture);
    glBindTexture(GL_TEXTURE_2D, g_farTexture);
    ConfigureTexture(GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_VALUE, halfWidth, halfHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_VALUE, &previousFramebuffer);
    g_glGenFramebuffers(1, &g_framebuffer);
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_framebuffer);
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_nearTexture, 0);
    const bool complete = g_glCheckFramebufferStatus(GL_FRAMEBUFFER_VALUE) ==
                          GL_FRAMEBUFFER_COMPLETE_VALUE;
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(previousFramebuffer));
    if (!complete) return false;
    g_textureWidth = width;
    g_textureHeight = height;
    return true;
}

bool ReadMainCamera(const ShaderInjectFrameContext* frame,
                    float& nearPlane, float& farPlane) {
    if (!frame ||
        frame->structSize < SHADER_INJECT_FRAME_CONTEXT_CAMERA_SIZE ||
        !frame->cameraDataValid || !frame->scene) return false;
    nearPlane = frame->nearPlane;
    farPlane = frame->farPlane;
    return frame->fovY > 0.0f && frame->fovY < 179.0f &&
           nearPlane > 0.0f && farPlane > nearPlane &&
           frame->viewportWidth == 0 && frame->viewportHeight == 0;
}

float LinearizeDepth(float depth, float nearPlane, float farPlane) {
    const float ndc = depth * 2.0f - 1.0f;
    return (2.0f * nearPlane * farPlane) /
           (farPlane + nearPlane - ndc * (farPlane - nearPlane));
}

bool SampleMedianDepth(float normalizedX, float normalizedY,
                       const GLint viewport[4], float nearPlane, float farPlane,
                       float& distance) {
    if (viewport[2] < 3 || viewport[3] < 3) return false;
    const int centerX = viewport[0] + static_cast<int>(normalizedX * (viewport[2] - 1));
    const int centerY = viewport[1] + static_cast<int>(normalizedY * (viewport[3] - 1));
    const int x = (std::max)(viewport[0], (std::min)(centerX - 1, viewport[0] + viewport[2] - 3));
    const int y = (std::max)(viewport[1], (std::min)(centerY - 1, viewport[1] + viewport[3] - 3));
    float samples[9] = {};
    glReadPixels(x, y, 3, 3, GL_DEPTH_COMPONENT, GL_FLOAT, samples);
    float valid[9] = {};
    int count = 0;
    for (float sample : samples) {
        if (sample > 0.0f && sample < 0.9999f) valid[count++] = sample;
    }
    if (count < 3) return false;
    std::sort(valid, valid + count);
    distance = LinearizeDepth(valid[count / 2], nearPlane, farPlane);
    return std::isfinite(distance) && distance > nearPlane && distance < farPlane;
}

void UpdateFocus(float target) {
    const DWORD now = GetTickCount();
    const DWORD elapsed = now - g_lastFocusTick;
    if (!g_focusValid) {
        g_focusDistance = target;
    }
    else {
        const float seconds = (std::min)(elapsed, static_cast<DWORD>(250)) * 0.001f;
        const float alpha = 1.0f - std::exp(-seconds / kFocusResponseSeconds);
        g_focusDistance += (target - g_focusDistance) * alpha;
    }
    g_focusValid = true;
    g_lastFocusTick = now;
}

float RefineFocus(const ShaderInjectDof::DialogueFocusTarget& focusTarget,
                  const GLint viewport[4], float nearPlane, float farPlane) {
    float focusDistance = focusTarget.distance;
    float visibleDistance = 0.0f;
    if (SampleMedianDepth(focusTarget.screenX, focusTarget.screenY, viewport,
                          nearPlane, farPlane, visibleDistance) &&
        std::fabs(visibleDistance - focusTarget.distance) /
                focusTarget.distance < 0.25f) {
        focusDistance = visibleDistance;
    }
    return focusDistance;
}

void SetCommonUniforms(GLuint program, float nearPlane, float farPlane,
                       float focusDistance, int width, int height) {
    g_glUniform1i(g_glGetUniformLocation(program, "colorTexture"), 0);
    g_glUniform1i(g_glGetUniformLocation(program, "depthTexture"), 1);
    g_glUniform2f(g_glGetUniformLocation(program, "inverseResolution"),
                  1.0f / width, 1.0f / height);
    g_glUniform1f(g_glGetUniformLocation(program, "nearPlane"), nearPlane);
    g_glUniform1f(g_glGetUniformLocation(program, "farPlane"), farPlane);
    g_glUniform1f(g_glGetUniformLocation(program, "focusDistance"), focusDistance);
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
    GLint textures[4] = {};
    bool captured = false;
    bool attribPushed = false;
};

void RestoreGlState(SavedGlState& state) {
    if (!state.captured) return;
    g_glUseProgram(static_cast<GLuint>(state.program));
    if (state.attribPushed) glPopAttrib();
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(state.framebuffer));
    for (int unit = 0; unit < 4; ++unit) {
        g_glActiveTexture(GL_TEXTURE0_VALUE + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures[unit]));
    }
    g_glActiveTexture(static_cast<GLenum>(state.activeTexture));
    state.captured = false;
    state.attribPushed = false;
}

bool DrawDepthOfField(float nearPlane, float farPlane, float focusDistance,
                      const GLint viewport[4], SavedGlState& state) {
    const int x = viewport[0];
    const int y = viewport[1];
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0 || !wglGetCurrentContext() || !LoadFunctions()) return false;

    glGetIntegerv(GL_CURRENT_PROGRAM_VALUE, &state.program);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_VALUE, &state.framebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &state.activeTexture);
    for (int unit = 0; unit < 4; ++unit) {
        g_glActiveTexture(GL_TEXTURE0_VALUE + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[unit]);
    }
    state.captured = true;
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    if (!EnsureResources(width, height)) {
        RestoreGlState(state);
        return false;
    }

    g_stage = "copy final scene color and depth";
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, width, height);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
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

    const int halfWidth = (width + 1) / 2;
    const int halfHeight = (height + 1) / 2;
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_framebuffer);
    glViewport(0, 0, halfWidth, halfHeight);
    g_glUseProgram(g_blurProgram);
    SetCommonUniforms(g_blurProgram, nearPlane, farPlane,
                      focusDistance, width, height);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);

    g_stage = "horizontal Gaussian blur";
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_nearTexture, 0);
    g_glUniform1i(g_glGetUniformLocation(g_blurProgram, "horizontalPass"), 1);
    g_glUniform1f(g_glGetUniformLocation(g_blurProgram, "sampleRadius"), 3.0f);
    DrawFullscreenQuad();

    g_stage = "vertical Gaussian blur";
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_farTexture, 0);
    g_glUniform1i(g_glGetUniformLocation(g_blurProgram, "horizontalPass"), 0);
    g_glUniform1f(g_glGetUniformLocation(g_blurProgram, "sampleRadius"), 1.5f);
    g_glUniform2f(g_glGetUniformLocation(g_blurProgram, "inverseResolution"),
                  1.0f / halfWidth, 1.0f / halfHeight);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_nearTexture);
    DrawFullscreenQuad();

    g_stage = "depth-of-field composite";
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, static_cast<GLuint>(state.framebuffer));
    glViewport(x, y, width, height);
    g_glUseProgram(g_compositeProgram);
    g_glUniform1i(g_glGetUniformLocation(g_compositeProgram, "colorTexture"), 0);
    g_glUniform1i(g_glGetUniformLocation(g_compositeProgram, "depthTexture"), 1);
    g_glUniform1i(g_glGetUniformLocation(g_compositeProgram, "farTexture"), 3);
    g_glUniform1f(g_glGetUniformLocation(g_compositeProgram, "nearPlane"), nearPlane);
    g_glUniform1f(g_glGetUniformLocation(g_compositeProgram, "farPlane"), farPlane);
    g_glUniform1f(g_glGetUniformLocation(g_compositeProgram, "focusDistance"), focusDistance);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
    g_glActiveTexture(GL_TEXTURE3_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_farTexture);
    DrawFullscreenQuad();

    RestoreGlState(state);
    g_stage = "idle";
    return true;
}

int HandleException(EXCEPTION_POINTERS* exception) {
    char message[256] = {};
    sprintf_s(message, "[DOF] Exception 0x%08lX at %p during '%s'.",
              exception->ExceptionRecord->ExceptionCode,
              exception->ExceptionRecord->ExceptionAddress, g_stage);
    Log(message);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

namespace ShaderInjectDof {

void ResetDepthOfField() {
    g_focusValid = false;
}

void ApplyDepthOfField(const ShaderInjectFrameContext* frame,
                       const DialogueFocusTarget& focusTarget) {
    if (!frame) {
        ResetDepthOfField();
        return;
    }
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    if (!ReadMainCamera(frame, nearPlane, farPlane)) {
        ResetDepthOfField();
        return;
    }
    SavedGlState savedState;
    __try {
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const float target = RefineFocus(
            focusTarget, viewport, nearPlane, farPlane);
        UpdateFocus(target);
        if (!DrawDepthOfField(nearPlane, farPlane, g_focusDistance, viewport,
                              savedState)) {
            if (!g_failureLogged) {
                g_failureLogged = true;
                Log("[DOF] OpenGL shader or framebuffer resources are unavailable.");
            }
            return;
        }
        if (!g_firstFrameLogged) {
            g_firstFrameLogged = true;
            Log("[DOF] First cinematic-dialogue composite completed.");
        }
    }
    __except (HandleException(GetExceptionInformation())) {
        RestoreGlState(savedState);
        ResetDepthOfField();
    }
}

} // namespace ShaderInjectDof
