#pragma once

#include <windows.h>

#include <cstdint>

constexpr std::uint32_t SHADER_INJECT_FULLSCREEN_ABI_VERSION = 1;

struct ShaderInjectFullscreenEffectV1 {
    std::uint32_t structSize;
    std::uint32_t abiVersion;
    const char* effectId;
    const char* fragmentShaderSource;
    const char* frameIndexUniform;
};

using ShaderInjectApplyFullscreenEffectFn = BOOL (__cdecl*)(
    const ShaderInjectFullscreenEffectV1* effect);
