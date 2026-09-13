#pragma once

#include <windows.h>

#include <cstdint>

constexpr std::uint32_t SHADER_INJECT_REPLACEMENT_ABI_VERSION = 1;

struct ShaderInjectShaderReplacement {
    unsigned int target;
    std::uint64_t originalHash;
    const void* source;
    unsigned int sourceSize;
};

struct ShaderInjectReplacementProviderV1 {
    std::uint32_t structSize;
    std::uint32_t abiVersion;
    const char* providerId;
    const ShaderInjectShaderReplacement* replacements;
    unsigned int replacementCount;
};

using ShaderInjectRegisterReplacementProviderFn = BOOL (__cdecl*)(
    const ShaderInjectReplacementProviderV1* provider);
