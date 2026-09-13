#include <windows.h>
#include <GL/gl.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../ShaderInject/ShaderInjectDepthControl.h"
#include "../ShaderInject/ShaderInjectProvider.h"

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "user32.lib")

namespace {

using GLchar = char;

constexpr GLenum GL_ACTIVE_TEXTURE_VALUE = 0x84E0;
constexpr GLenum GL_COMPILE_STATUS_VALUE = 0x8B81;
constexpr GLenum GL_CURRENT_PROGRAM_VALUE = 0x8B8D;
constexpr GLenum GL_DEPTH_COMPONENT24_VALUE = 0x81A6;
constexpr GLenum GL_FRAMEBUFFER_BINDING_VALUE = 0x8CA6;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE_VALUE = 0x8CD5;
constexpr GLenum GL_FRAMEBUFFER_VALUE = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0_VALUE = 0x8CE0;
constexpr GLenum GL_FRAGMENT_SHADER_VALUE = 0x8B30;
constexpr GLenum GL_LINK_STATUS_VALUE = 0x8B82;
constexpr GLenum GL_RGB8_VALUE = 0x8051;
constexpr GLenum GL_TEXTURE0_VALUE = 0x84C0;
constexpr GLenum GL_TEXTURE1_VALUE = 0x84C1;
constexpr GLenum GL_TEXTURE2_VALUE = 0x84C2;
constexpr GLenum GL_VERTEX_SHADER_VALUE = 0x8B31;

constexpr size_t kNodeMaterialOffset = 0x44;
constexpr size_t kTextureGetNameVtableOffset = 0x40;
constexpr size_t kTextureNameLength = 16;
constexpr float kDegreesToHalfRadians = 0.008726646259971648f;

using TextureGetNameFn = const char* (__thiscall*)(const void* texture);

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

bool g_enabled = true;
bool g_f10Down = false;
bool g_firstFrameLogged = false;
HGLRC g_resourceContext = nullptr;
GLuint g_depthTexture = 0;
GLuint g_colorTexture = 0;
GLuint g_aoTextures[2] = {};
GLuint g_framebuffer = 0;
GLuint g_program = 0;
GLuint g_blurProgram = 0;
int g_textureWidth = 0;
int g_textureHeight = 0;
bool g_functionsLoaded = false;
bool g_failureLogged = false;
const char* g_postProcessStage = "idle";
bool g_decalMeshActive = false;
ShaderInjectBeginDepthWriteSuppressionFn g_beginDepthWriteSuppression = nullptr;
ShaderInjectEndDepthWriteSuppressionFn g_endDepthWriteSuppression = nullptr;
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

const char* kVertexShader = R"GLSL(
#version 120
varying vec2 vUv;
void main() {
    gl_Position = gl_Vertex;
    vUv = gl_MultiTexCoord0.xy;
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#version 120
uniform sampler2D depthTexture;
uniform vec2 inverseResolution;
uniform float nearPlane;
uniform float farPlane;
uniform float tanHalfFov;
uniform float aspectRatio;
varying vec2 vUv;

float viewDepth(vec2 uv) {
    float depth = texture2D(depthTexture, uv).r;
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
        (farPlane + nearPlane - ndc * (farPlane - nearPlane));
}

vec3 viewPosition(vec2 uv) {
    float z = viewDepth(uv);
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * aspectRatio * tanHalfFov * z,
                ndc.y * tanHalfFov * z, -z);
}

vec3 viewNormal(vec2 uv, vec3 position) {
    vec3 left = viewPosition(clamp(uv - vec2(inverseResolution.x, 0.0),
                                   vec2(0.001), vec2(0.999)));
    vec3 right = viewPosition(clamp(uv + vec2(inverseResolution.x, 0.0),
                                    vec2(0.001), vec2(0.999)));
    vec3 down = viewPosition(clamp(uv - vec2(0.0, inverseResolution.y),
                                   vec2(0.001), vec2(0.999)));
    vec3 up = viewPosition(clamp(uv + vec2(0.0, inverseResolution.y),
                                 vec2(0.001), vec2(0.999)));
    vec3 dxRight = right - position;
    vec3 dxLeft = position - left;
    vec3 dyUp = up - position;
    vec3 dyDown = position - down;
    vec3 dx = abs(dxRight.z) < abs(dxLeft.z) ? dxRight : dxLeft;
    vec3 dy = abs(dyUp.z) < abs(dyDown.z) ? dyUp : dyDown;
    return normalize(cross(dx, dy));
}

float sampleOcclusion(vec3 position, vec3 normal, mat2 rotation,
                      vec2 direction, float scale, float uvRadius, float radius) {
    vec2 uv = clamp(vUv + rotation * direction * (uvRadius * scale),
                    vec2(0.001), vec2(0.999));
    float sampleDepth = texture2D(depthTexture, uv).r;
    if (sampleDepth >= 0.99999) return 0.0;
    vec3 samplePosition = viewPosition(uv);
    vec3 delta = samplePosition - position;
    float distanceToSample = length(delta);
    float normalSeparation = dot(normal, delta);
    if (normalSeparation <= 0.06) return 0.0;
    float thicknessWeight = smoothstep(0.06, 0.12, normalSeparation);
    float normalAlignment = dot(normal, viewNormal(uv, samplePosition));
    float parallelWeight = 1.0 - smoothstep(0.88, 0.97, normalAlignment);
    float facing = max(dot(normal, delta / max(distanceToSample, 0.0001)) - 0.06, 0.0);
    float rangeWeight = 1.0 - smoothstep(radius * 0.55, radius, distanceToSample);
    return facing * rangeWeight * thicknessWeight * parallelWeight;
}

void main() {
    float rawDepth = texture2D(depthTexture, vUv).r;
    if (rawDepth >= 0.99999) {
        gl_FragColor = vec4(1.0);
        return;
    }

    vec3 position = viewPosition(vUv);
    vec3 normal = viewNormal(vUv, position);

    const float radius = 0.40;
    float uvRadius = clamp(radius / max(-position.z * tanHalfFov * 2.0, 0.001),
                           1.5 * inverseResolution.y, 0.025);
    float angle = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) *
                        43758.5453) * 6.2831853;
    mat2 rotation = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));

    float occlusion = 0.0;
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 1.000,  0.000), 0.25, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2(-0.737,  0.675), 0.34, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.087, -0.996), 0.43, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.608,  0.794), 0.52, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2(-0.985, -0.174), 0.60, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.844, -0.537), 0.68, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2(-0.259,  0.966), 0.75, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2(-0.461, -0.887), 0.82, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.939,  0.343), 0.88, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2(-0.924,  0.383), 0.93, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.424, -0.906), 0.97, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.300,  0.954), 1.00, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2(-0.681, -0.732), 0.40, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.996, -0.087), 0.58, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2(-0.793,  0.609), 0.78, uvRadius, radius);
    occlusion += sampleOcclusion(position, normal, rotation, vec2( 0.181, -0.984), 0.91, uvRadius, radius);

    float ao = mix(1.0,
                   clamp(1.0 - (occlusion / 16.0) * 1.35, 0.60, 1.0),
                   0.5);
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
)GLSL";

const char* kBlurFragmentShader = R"GLSL(
#version 120
uniform sampler2D aoTexture;
uniform sampler2D depthTexture;
uniform sampler2D colorTexture;
uniform vec2 inverseResolution;
uniform vec2 blurDirection;
uniform float nearPlane;
uniform float farPlane;
uniform int protectEmissive;
varying vec2 vUv;

float viewDepth(vec2 uv) {
    float depth = texture2D(depthTexture, uv).r;
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
        (farPlane + nearPlane - ndc * (farPlane - nearPlane));
}

float bilateralSample(vec2 uv, float centerDepth, float gaussianWeight,
                      inout float weightSum) {
    float sampleDepth = viewDepth(uv);
    float relativeDifference = abs(sampleDepth - centerDepth) /
                               max(centerDepth, 0.001);
    float weight = gaussianWeight * exp(-relativeDifference * 180.0);
    weightSum += weight;
    return texture2D(aoTexture, uv).r * weight;
}

void main() {
    float centerDepth = viewDepth(vUv);
    vec2 stepUv = blurDirection * inverseResolution;
    float weightSum = 0.0;
    float ao = 0.0;
    ao += bilateralSample(clamp(vUv - stepUv * 2.0, vec2(0.001), vec2(0.999)), centerDepth, 0.0625, weightSum);
    ao += bilateralSample(clamp(vUv - stepUv,       vec2(0.001), vec2(0.999)), centerDepth, 0.2500, weightSum);
    ao += bilateralSample(vUv, centerDepth, 0.3750, weightSum);
    ao += bilateralSample(clamp(vUv + stepUv,       vec2(0.001), vec2(0.999)), centerDepth, 0.2500, weightSum);
    ao += bilateralSample(clamp(vUv + stepUv * 2.0, vec2(0.001), vec2(0.999)), centerDepth, 0.0625, weightSum);
    ao /= max(weightSum, 0.0001);

    if (protectEmissive != 0) {
        vec3 sceneColor = texture2D(colorTexture, vUv).rgb;
        float luminance = dot(sceneColor, vec3(0.2126, 0.7152, 0.0722));
        ao = mix(ao, 1.0, smoothstep(0.55, 0.90, luminance));
    }
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
)GLSL";

void Log(const char* message) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* separator = std::strrchr(path, '\\');
    if (separator) strcpy_s(separator + 1, MAX_PATH - (separator + 1 - path), "ssao.log");
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") == 0 && file) {
        std::fprintf(file, "%s\n", message);
        std::fclose(file);
    }
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

bool ValidExtensionPointer(PROC pointer) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
    return pointer && value != 1 && value != 2 && value != 3 && value != static_cast<uintptr_t>(-1);
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

    char log[1024] = {};
    g_glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    Log(log[0] ? log : "[SSAO] Shader compilation failed.");
    g_glDeleteShader(shader);
    return 0;
}

bool CreateProgram(const char* fragmentSource, GLuint& program) {
    g_postProcessStage = "compile vertex shader";
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER_VALUE, kVertexShader);
    g_postProcessStage = "compile fragment shader";
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER_VALUE, fragmentSource);
    if (!vertexShader || !fragmentShader) {
        if (vertexShader) g_glDeleteShader(vertexShader);
        if (fragmentShader) g_glDeleteShader(fragmentShader);
        return false;
    }

    g_postProcessStage = "link shader program";
    program = g_glCreateProgram();
    g_glAttachShader(program, vertexShader);
    g_glAttachShader(program, fragmentShader);
    g_glLinkProgram(program);
    g_glDeleteShader(vertexShader);
    g_glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    g_glGetProgramiv(program, GL_LINK_STATUS_VALUE, &linked);
    if (linked == GL_TRUE) return true;

    char log[1024] = {};
    g_glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    Log(log[0] ? log : "[SSAO] Shader link failed.");
    g_glDeleteProgram(program);
    program = 0;
    return false;
}

bool EnsureResources(int width, int height) {
    g_postProcessStage = "query current OpenGL context";
    HGLRC context = wglGetCurrentContext();
    g_postProcessStage = "load OpenGL extension functions";
    if (!context || !LoadFunctions()) return false;
    if (context != g_resourceContext) {
        g_resourceContext = context;
        g_depthTexture = 0;
        g_colorTexture = 0;
        g_aoTextures[0] = 0;
        g_aoTextures[1] = 0;
        g_framebuffer = 0;
        g_program = 0;
        g_blurProgram = 0;
        g_textureWidth = 0;
        g_textureHeight = 0;
    }
    if (!g_program && !CreateProgram(kFragmentShader, g_program)) return false;
    if (!g_blurProgram && !CreateProgram(kBlurFragmentShader, g_blurProgram)) return false;
    if (g_depthTexture && g_colorTexture && g_aoTextures[0] &&
        g_aoTextures[1] && g_framebuffer &&
        width == g_textureWidth && height == g_textureHeight) return true;

    g_postProcessStage = "allocate SSAO textures";
    if (g_depthTexture) glDeleteTextures(1, &g_depthTexture);
    if (g_colorTexture) glDeleteTextures(1, &g_colorTexture);
    if (g_aoTextures[0]) glDeleteTextures(2, g_aoTextures);
    if (g_framebuffer) g_glDeleteFramebuffers(1, &g_framebuffer);
    g_depthTexture = 0;
    g_colorTexture = 0;
    g_aoTextures[0] = 0;
    g_aoTextures[1] = 0;
    g_framebuffer = 0;

    glGenTextures(1, &g_depthTexture);
    if (!g_depthTexture) return false;
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24_VALUE, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

    glGenTextures(1, &g_colorTexture);
    if (!g_colorTexture) return false;
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8_VALUE, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(2, g_aoTextures);
    if (!g_aoTextures[0] || !g_aoTextures[1]) return false;
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, g_aoTextures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8_VALUE, width, height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }

    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_VALUE, &previousFramebuffer);
    g_glGenFramebuffers(1, &g_framebuffer);
    if (!g_framebuffer) return false;
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_framebuffer);
    bool framebufferComplete = true;
    for (int i = 0; i < 2; ++i) {
        g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                                 GL_TEXTURE_2D, g_aoTextures[i], 0);
        framebufferComplete = framebufferComplete &&
            g_glCheckFramebufferStatus(GL_FRAMEBUFFER_VALUE) ==
                GL_FRAMEBUFFER_COMPLETE_VALUE;
    }
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(previousFramebuffer));
    if (!framebufferComplete) return false;

    g_textureWidth = width;
    g_textureHeight = height;
    return true;
}

bool ReadCamera(const ShaderInjectFrameContext* frame,
                float& fov, float& nearPlane, float& farPlane,
                int& viewportX, int& viewportY, int& viewportWidth, int& viewportHeight) {
    if (!frame ||
        frame->structSize < SHADER_INJECT_FRAME_CONTEXT_CAMERA_SIZE ||
        !frame->cameraDataValid || !frame->scene) return false;
    fov = frame->fovY;
    nearPlane = frame->nearPlane;
    farPlane = frame->farPlane;
    viewportX = frame->viewportX;
    viewportY = frame->viewportY;
    viewportWidth = frame->viewportWidth;
    viewportHeight = frame->viewportHeight;
    const bool fullViewport = viewportWidth == 0 && viewportHeight == 0;
    const bool explicitViewport = viewportWidth > 0 && viewportHeight > 0;
    return fov > 0.0f && fov < 179.0f && nearPlane > 0.0f &&
           farPlane > nearPlane && (fullViewport || explicitViewport);
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
    GLint activeTexture = GL_TEXTURE0_VALUE;
    GLint textures[3] = {};
    GLint framebuffer = 0;
    bool captured = false;
    bool attribPushed = false;
};

void RestoreGlState(SavedGlState& state) {
    if (!state.captured) return;
    g_glUseProgram(static_cast<GLuint>(state.program));
    if (state.attribPushed) glPopAttrib();
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(state.framebuffer));
    for (int unit = 2; unit >= 0; --unit) {
        g_glActiveTexture(GL_TEXTURE0_VALUE + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures[unit]));
    }
    g_glActiveTexture(static_cast<GLenum>(state.activeTexture));
    state.captured = false;
    state.attribPushed = false;
}

bool DrawSsao(float fov, float nearPlane, float farPlane,
              int requestedX, int requestedY, int requestedWidth,
              int requestedHeight, SavedGlState& state) {
    g_postProcessStage = "query viewport";
    GLint originalViewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, originalViewport);
    const bool explicitViewport = requestedWidth > 0 && requestedHeight > 0;
    const int x = explicitViewport ? requestedX : originalViewport[0];
    const int y = explicitViewport ? requestedY : originalViewport[1];
    const int width = explicitViewport ? requestedWidth : originalViewport[2];
    const int height = explicitViewport ? requestedHeight : originalViewport[3];
    if (width <= 0 || height <= 0) return false;
    if (x < originalViewport[0] || y < originalViewport[1] ||
        x + width > originalViewport[0] + originalViewport[2] ||
        y + height > originalViewport[1] + originalViewport[3]) return false;

    g_postProcessStage = "load OpenGL extension functions";
    if (!wglGetCurrentContext() || !LoadFunctions()) {
        if (!g_failureLogged) {
            g_failureLogged = true;
            Log("[SSAO] OpenGL 2.0 extension functions are unavailable.");
        }
        return false;
    }

    g_postProcessStage = "capture OpenGL state";
    glGetIntegerv(GL_CURRENT_PROGRAM_VALUE, &state.program);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_VALUE, &state.framebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE_VALUE, &state.activeTexture);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[0]);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[1]);
    g_glActiveTexture(GL_TEXTURE2_VALUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[2]);
    state.captured = true;
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    g_postProcessStage = "ensure SSAO resources";
    if (!EnsureResources(width, height)) {
        RestoreGlState(state);
        if (!g_failureLogged) {
            g_failureLogged = true;
            Log("[SSAO] OpenGL framebuffer or shader resources unavailable; effect disabled for this frame.");
        }
        return false;
    }

    g_postProcessStage = "copy scene color and depth";
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, width, height);
    g_glActiveTexture(GL_TEXTURE2_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, width, height);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);

    g_postProcessStage = "configure SSAO render state";
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    state.attribPushed = true;
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);

    g_postProcessStage = "render raw SSAO";
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE, g_framebuffer);
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_aoTextures[0], 0);
    g_glUseProgram(g_program);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float tanHalfFov = std::tan(fov * kDegreesToHalfRadians);
    g_glUniform1i(g_glGetUniformLocation(g_program, "depthTexture"), 0);
    g_glUniform2f(g_glGetUniformLocation(g_program, "inverseResolution"),
                  1.0f / width, 1.0f / height);
    g_glUniform1f(g_glGetUniformLocation(g_program, "nearPlane"), nearPlane);
    g_glUniform1f(g_glGetUniformLocation(g_program, "farPlane"), farPlane);
    g_glUniform1f(g_glGetUniformLocation(g_program, "tanHalfFov"), tanHalfFov);
    g_glUniform1f(g_glGetUniformLocation(g_program, "aspectRatio"), aspect);
    DrawFullscreenQuad();

    g_postProcessStage = "blur SSAO horizontally";
    g_glFramebufferTexture2D(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                             GL_TEXTURE_2D, g_aoTextures[1], 0);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_aoTextures[0]);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    g_glUseProgram(g_blurProgram);
    g_glUniform1i(g_glGetUniformLocation(g_blurProgram, "aoTexture"), 1);
    g_glUniform1i(g_glGetUniformLocation(g_blurProgram, "depthTexture"), 0);
    g_glUniform1i(g_glGetUniformLocation(g_blurProgram, "colorTexture"), 2);
    g_glUniform2f(g_glGetUniformLocation(g_blurProgram, "inverseResolution"),
                  1.0f / width, 1.0f / height);
    g_glUniform1f(g_glGetUniformLocation(g_blurProgram, "nearPlane"), nearPlane);
    g_glUniform1f(g_glGetUniformLocation(g_blurProgram, "farPlane"), farPlane);
    g_glUniform2f(g_glGetUniformLocation(g_blurProgram, "blurDirection"), 1.0f, 0.0f);
    g_glUniform1i(g_glGetUniformLocation(g_blurProgram, "protectEmissive"), 0);
    DrawFullscreenQuad();

    g_postProcessStage = "blur and composite SSAO vertically";
    g_glBindFramebuffer(GL_FRAMEBUFFER_VALUE,
                        static_cast<GLuint>(state.framebuffer));
    glViewport(x, y, width, height);
    g_glActiveTexture(GL_TEXTURE1_VALUE);
    glBindTexture(GL_TEXTURE_2D, g_aoTextures[1]);
    g_glActiveTexture(GL_TEXTURE0_VALUE);
    g_glUniform2f(g_glGetUniformLocation(g_blurProgram, "blurDirection"), 0.0f, 1.0f);
    g_glUniform1i(g_glGetUniformLocation(g_blurProgram, "protectEmissive"), 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    DrawFullscreenQuad();

    g_postProcessStage = "restore OpenGL state";
    RestoreGlState(state);
    return true;
}

int HandleSsaoException(EXCEPTION_POINTERS* exception) {
    char message[256] = {};
    sprintf_s(message, "[SSAO] Exception 0x%08lX at %p during '%s'.",
              exception->ExceptionRecord->ExceptionCode,
              exception->ExceptionRecord->ExceptionAddress,
              g_postProcessStage);
    Log(message);
    return EXCEPTION_EXECUTE_HANDLER;
}

enum class SsaoResult {
    Success,
    Unavailable,
    Exception,
};

SsaoResult RunSsaoGuarded(float fov, float nearPlane, float farPlane,
                          int viewportX, int viewportY,
                          int viewportWidth, int viewportHeight) {
    SavedGlState savedState;
    __try {
        return DrawSsao(fov, nearPlane, farPlane,
                        viewportX, viewportY, viewportWidth, viewportHeight,
                        savedState)
            ? SsaoResult::Success
            : SsaoResult::Unavailable;
    }
    __except (HandleSsaoException(GetExceptionInformation())) {
        RestoreGlState(savedState);
        return SsaoResult::Exception;
    }
}

void PollToggle() {
    const bool f10Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (f10Down && !g_f10Down) {
        g_enabled = !g_enabled;
        Log(g_enabled ? "[SSAO] Enabled by F10." : "[SSAO] Disabled by F10.");
    }
    g_f10Down = f10Down;
}

bool IsBlastDecalTexture(const char* name) {
    if (!name) return false;
    for (size_t i = 0; i + 5 < kTextureNameLength && name[i]; ++i) {
        if (name[i] != '_') continue;
        const char b = static_cast<char>(name[i + 1] | 0x20);
        const char l = static_cast<char>(name[i + 2] | 0x20);
        const char s = static_cast<char>(name[i + 3] | 0x20);
        const char t = static_cast<char>(name[i + 4] | 0x20);
        const char suffix = name[i + 5];
        if (b == 'b' && l == 'l' && s == 's' && t == 't' &&
            suffix >= '0' && suffix <= '9') {
            return true;
        }
    }
    return false;
}

bool IsBlastDecalMesh(void* node) {
    if (!node) return false;
    __try {
        const auto* nodeBytes = static_cast<const unsigned char*>(node);
        const auto* material = *reinterpret_cast<const unsigned char* const*>(
            nodeBytes + kNodeMaterialOffset);
        if (!material) return false;
        const auto* texture = *reinterpret_cast<const unsigned char* const*>(material);
        if (!texture) return false;
        const auto* vtable = *reinterpret_cast<void* const* const*>(texture);
        if (!vtable) return false;
        const auto getName = reinterpret_cast<TextureGetNameFn>(
            vtable[kTextureGetNameVtableOffset / sizeof(void*)]);
        return getName && IsBlastDecalTexture(getName(texture));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void __cdecl ShaderInjectSsaoBeforeParticles(
    const ShaderInjectFrameContext* frame) {
    PollToggle();
    if (!frame || !g_enabled) return;
    float fov = 0.0f;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    int viewportX = 0;
    int viewportY = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
    if (!ReadCamera(frame, fov, nearPlane, farPlane,
                    viewportX, viewportY, viewportWidth, viewportHeight)) return;
    const bool firstFrame = !g_firstFrameLogged;
    const SsaoResult result = RunSsaoGuarded(
        fov, nearPlane, farPlane,
        viewportX, viewportY, viewportWidth, viewportHeight);
    if (result == SsaoResult::Exception) {
        g_enabled = false;
        Log("[SSAO] OpenGL post-process was disabled after the exception.");
    }
    else if (result == SsaoResult::Success && firstFrame) {
        g_firstFrameLogged = true;
        Log("[SSAO] First ShaderInject pre-particle composite completed.");
    }
}

void __cdecl ShaderInjectSsaoBeginMesh(void* node) {
    if (!g_enabled || !g_beginDepthWriteSuppression ||
        !IsBlastDecalMesh(node)) return;
    g_decalMeshActive = g_beginDepthWriteSuppression() != FALSE;
}

void __cdecl ShaderInjectSsaoEndMesh() {
    if (!g_decalMeshActive || !g_endDepthWriteSuppression) return;
    g_endDepthWriteSuppression();
    g_decalMeshActive = false;
}

extern "C" BOOL __cdecl KPatch_Initialize() {
    static LONG initialized = 0;
    if (InterlockedCompareExchange(&initialized, 1, 0) != 0) return TRUE;

    HMODULE shaderInject = GetModuleHandleA("shader-inject.dll");
    if (!shaderInject) {
        InterlockedExchange(&initialized, 0);
        return FALSE;
    }
    g_beginDepthWriteSuppression =
        reinterpret_cast<ShaderInjectBeginDepthWriteSuppressionFn>(
            GetProcAddress(
                shaderInject, "ShaderInject_BeginDepthWriteSuppression"));
    g_endDepthWriteSuppression =
        reinterpret_cast<ShaderInjectEndDepthWriteSuppressionFn>(
            GetProcAddress(
                shaderInject, "ShaderInject_EndDepthWriteSuppression"));
    if (!g_beginDepthWriteSuppression || !g_endDepthWriteSuppression) {
        InterlockedExchange(&initialized, 0);
        return FALSE;
    }
    auto registerProvider = reinterpret_cast<ShaderInjectRegisterProviderFn>(
        GetProcAddress(shaderInject, "ShaderInject_RegisterProvider"));
    const ShaderInjectProviderV1 provider = {
        sizeof(ShaderInjectProviderV1),
        SHADER_INJECT_ABI_VERSION,
        nullptr,
        &ShaderInjectSsaoBeforeParticles,
        nullptr,
        &ShaderInjectSsaoBeginMesh,
        &ShaderInjectSsaoEndMesh,
        SHADER_INJECT_PRIORITY_EARLY,
    };
    if (!registerProvider || !registerProvider(&provider)) {
        InterlockedExchange(&initialized, 0);
        return FALSE;
    }

    Log("[SSAO] ShaderInject provider loaded. SSAO is enabled; press F10 to toggle.");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        return KPatch_Initialize();
    }
    return TRUE;
}
