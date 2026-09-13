#pragma once

#include <windows.h>

#include <cstdint>
#include <cstddef>

constexpr std::uint32_t SHADER_INJECT_ABI_VERSION = 1;

enum ShaderInjectGame : std::uint32_t {
    SHADER_INJECT_GAME_UNKNOWN = 0,
    SHADER_INJECT_GAME_KOTOR1 = 1,
    SHADER_INJECT_GAME_KOTOR2 = 2,
};

enum ShaderInjectPriority : int {
    SHADER_INJECT_PRIORITY_EARLY = 0,
    SHADER_INJECT_PRIORITY_DEPTH_OF_FIELD = 200,
    SHADER_INJECT_PRIORITY_ANTI_ALIASING = 300,
    SHADER_INJECT_PRIORITY_VIGNETTE = 350,
    SHADER_INJECT_PRIORITY_FILM_GRAIN = 400,
};

struct ShaderInjectFrameContext {
    std::uint32_t structSize;
    void* camera;
    void* scene;
    BOOL matricesValid;
    float modelView[16];
    float projection[16];
    std::uint32_t game;
    BOOL cameraDataValid;
    float fovY;
    float nearPlane;
    float farPlane;
    int viewportX;
    int viewportY;
    int viewportWidth;
    int viewportHeight;
};

constexpr std::uint32_t SHADER_INJECT_FRAME_CONTEXT_CAMERA_SIZE =
    static_cast<std::uint32_t>(sizeof(ShaderInjectFrameContext));

inline bool ShaderInject_IsMainPerspectiveCamera(
    const ShaderInjectFrameContext* frame) {
    return frame &&
           frame->structSize >= SHADER_INJECT_FRAME_CONTEXT_CAMERA_SIZE &&
           frame->cameraDataValid && frame->scene &&
           frame->fovY > 0.0f && frame->fovY < 179.0f &&
           frame->viewportWidth == 0 && frame->viewportHeight == 0;
}

struct ShaderInjectProviderV1 {
    std::uint32_t structSize;
    std::uint32_t abiVersion;
    void (__cdecl* onFrameBegin)(const ShaderInjectFrameContext* frame);
    void (__cdecl* onBeforeParticles)(const ShaderInjectFrameContext* frame);
    void (__cdecl* onFinalScene)(const ShaderInjectFrameContext* frame);
    void (__cdecl* onMeshBegin)(void* node);
    void (__cdecl* onMeshEnd)();
    int priority;
};

constexpr std::uint32_t SHADER_INJECT_PROVIDER_V1_BASE_SIZE =
    static_cast<std::uint32_t>(offsetof(ShaderInjectProviderV1, priority));

using ShaderInjectRegisterProviderFn = BOOL (__cdecl*)(
    const ShaderInjectProviderV1* provider);
