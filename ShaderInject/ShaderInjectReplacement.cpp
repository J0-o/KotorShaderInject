#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ShaderInjectReplacementProvider.h"
#include "X86InlineDetour.h"

namespace {

using GlProgramStringArbFn = void (WINAPI*)(
    unsigned int target, unsigned int format, int len, const void* source);
using WglGetProcAddressFn = PROC (WINAPI*)(LPCSTR name);

constexpr unsigned int kProgramFormatAsciiArb = 0x8875;
constexpr unsigned int kVertexProgramArb = 0x8620;
constexpr unsigned int kFragmentProgramArb = 0x8804;
constexpr unsigned int kMaxProviders = 64;
constexpr LONG kHookNotStarted = 0;
constexpr LONG kHookStarting = 1;
constexpr LONG kHookInstalled = 2;
constexpr LONG kHookFailed = 3;

enum class HookMode {
    Uninitialized,
    Detoured,
    WrapperFallback,
};

WglGetProcAddressFn g_originalWglGetProcAddress = nullptr;
GlProgramStringArbFn g_originalGlProgramStringArb = nullptr;
x86hook::InlineDetour g_wglGetProcAddressDetour;
x86hook::InlineDetour g_programStringDetour;
HookMode g_programStringHookMode = HookMode::Uninitialized;
SRWLOCK g_detourLock = SRWLOCK_INIT;

ShaderInjectReplacementProviderV1 g_providers[kMaxProviders]{};
unsigned int g_providerCount = 0;
SRWLOCK g_providerLock = SRWLOCK_INIT;
LONG g_hookState = kHookNotStarted;

void LogProviderCollision(const char* existingId, const char* rejectedId) {
    OutputDebugStringA("[ShaderInject] Replacement collision between providers '");
    OutputDebugStringA(existingId);
    OutputDebugStringA("' and '");
    OutputDebugStringA(rejectedId);
    OutputDebugStringA("'.\n");
}

std::uint64_t HashShaderSource(const char* source, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<unsigned char>(source[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

const ShaderInjectShaderReplacement* FindReplacement(
    unsigned int target, const char* source, std::size_t size) {
    const std::uint64_t hash = HashShaderSource(source, size);
    const ShaderInjectShaderReplacement* match = nullptr;
    AcquireSRWLockShared(&g_providerLock);
    for (unsigned int providerIndex = 0;
         providerIndex < g_providerCount && !match;
         ++providerIndex) {
        const auto& provider = g_providers[providerIndex];
        for (unsigned int replacementIndex = 0;
             replacementIndex < provider.replacementCount;
             ++replacementIndex) {
            const auto& replacement = provider.replacements[replacementIndex];
            if (replacement.target == target &&
                replacement.originalHash == hash) {
                match = &replacement;
                break;
            }
        }
    }
    ReleaseSRWLockShared(&g_providerLock);
    return match;
}

void WINAPI HookedGlProgramStringArb(
    unsigned int target, unsigned int format, int len, const void* source) {
    if (!g_originalGlProgramStringArb) {
        return;
    }

    if (format == kProgramFormatAsciiArb && source && len > 0) {
        const auto* replacement = FindReplacement(
            target, static_cast<const char*>(source),
            static_cast<std::size_t>(len));
        if (replacement) {
            g_originalGlProgramStringArb(
                target, format, static_cast<int>(replacement->sourceSize),
                replacement->source);
            return;
        }
    }

    g_originalGlProgramStringArb(target, format, len, source);
}

PROC ResolveProgramStringEntry(PROC resolved) {
    if (!resolved) {
        return nullptr;
    }

    AcquireSRWLockExclusive(&g_detourLock);
    PROC result = resolved;
    if (g_programStringHookMode == HookMode::Uninitialized) {
        if (g_programStringDetour.Install(
                reinterpret_cast<void*>(resolved),
                reinterpret_cast<void*>(&HookedGlProgramStringArb),
                reinterpret_cast<void* volatile*>(
                    &g_originalGlProgramStringArb))) {
            g_programStringHookMode = HookMode::Detoured;
        } else {
            g_originalGlProgramStringArb =
                reinterpret_cast<GlProgramStringArbFn>(resolved);
            g_programStringHookMode = HookMode::WrapperFallback;
            result = reinterpret_cast<PROC>(&HookedGlProgramStringArb);
        }
    } else if (
        g_programStringHookMode == HookMode::WrapperFallback &&
        reinterpret_cast<PROC>(g_originalGlProgramStringArb) == resolved) {
        result = reinterpret_cast<PROC>(&HookedGlProgramStringArb);
    }
    ReleaseSRWLockExclusive(&g_detourLock);
    return result;
}

PROC WINAPI HookedWglGetProcAddress(LPCSTR name) {
    if (!g_originalWglGetProcAddress) {
        return nullptr;
    }

    PROC resolved = g_originalWglGetProcAddress(name);
    if (name &&
        (std::strcmp(name, "glProgramStringARB") == 0 ||
         std::strcmp(name, "glProgramString") == 0)) {
        return ResolveProgramStringEntry(resolved);
    }
    return resolved;
}

bool InstallHook(HMODULE openGl) {
    if (!openGl) {
        return false;
    }

    void* wglGetProcAddress = reinterpret_cast<void*>(
        GetProcAddress(openGl, "wglGetProcAddress"));
    if (!g_wglGetProcAddressDetour.Install(
            wglGetProcAddress,
            reinterpret_cast<void*>(&HookedWglGetProcAddress),
            reinterpret_cast<void* volatile*>(
                &g_originalWglGetProcAddress))) {
        OutputDebugStringA(
            "[ShaderInject] Failed to intercept wglGetProcAddress.\n");
        return false;
    }
    OutputDebugStringA(
        "[ShaderInject] Shader replacement hook installed.\n");
    return true;
}

DWORD WINAPI WaitForOpenGlAndInstallHook(LPVOID) {
    HMODULE openGl = nullptr;
    for (int attempt = 0; attempt < 240 && !openGl; ++attempt) {
        openGl = GetModuleHandleA("opengl32.dll");
        if (!openGl) Sleep(250);
    }
    if (!openGl) {
        OutputDebugStringA(
            "[ShaderInject] Timed out waiting for opengl32.dll.\n");
        InterlockedExchange(&g_hookState, kHookFailed);
        return 0;
    }
    InterlockedExchange(
        &g_hookState,
        InstallHook(openGl) ? kHookInstalled : kHookFailed);
    return 0;
}

BOOL StartHook() {
    LONG state = InterlockedCompareExchange(
        &g_hookState, kHookStarting, kHookNotStarted);
    if (state == kHookInstalled || state == kHookStarting) return TRUE;
    if (state == kHookFailed &&
        InterlockedCompareExchange(
            &g_hookState, kHookStarting, kHookFailed) != kHookFailed) {
        return TRUE;
    }

    HMODULE openGl = GetModuleHandleA("opengl32.dll");
    if (openGl) {
        const bool installed = InstallHook(openGl);
        InterlockedExchange(
            &g_hookState, installed ? kHookInstalled : kHookFailed);
        return installed ? TRUE : FALSE;
    }

    HANDLE thread = CreateThread(
        nullptr, 0, &WaitForOpenGlAndInstallHook, nullptr, 0, nullptr);
    if (!thread) {
        InterlockedExchange(&g_hookState, kHookFailed);
        return FALSE;
    }
    CloseHandle(thread);
    return TRUE;
}

bool IsValidProvider(const ShaderInjectReplacementProviderV1* provider) {
    if (!provider ||
        provider->structSize < sizeof(ShaderInjectReplacementProviderV1) ||
        provider->abiVersion != SHADER_INJECT_REPLACEMENT_ABI_VERSION ||
        !provider->providerId || provider->providerId[0] == '\0' ||
        !provider->replacements || provider->replacementCount == 0) {
        return false;
    }

    for (unsigned int index = 0;
         index < provider->replacementCount;
         ++index) {
        const auto& replacement = provider->replacements[index];
        if ((replacement.target != kVertexProgramArb &&
             replacement.target != kFragmentProgramArb) ||
            !replacement.source || replacement.sourceSize == 0 ||
            replacement.sourceSize > 0x7fffffffu) {
            return false;
        }
    }
    return true;
}

} // namespace

extern "C" BOOL __cdecl ShaderInject_RegisterReplacementProvider(
    const ShaderInjectReplacementProviderV1* provider) {
    if (!IsValidProvider(provider)) {
        return FALSE;
    }

    AcquireSRWLockExclusive(&g_providerLock);
    if (g_providerCount >= kMaxProviders) {
        ReleaseSRWLockExclusive(&g_providerLock);
        return FALSE;
    }

    for (unsigned int newIndex = 0;
         newIndex < provider->replacementCount;
         ++newIndex) {
        const auto& candidate = provider->replacements[newIndex];
        for (unsigned int earlierIndex = 0;
             earlierIndex < newIndex;
             ++earlierIndex) {
            const auto& earlier = provider->replacements[earlierIndex];
            if (candidate.target == earlier.target &&
                candidate.originalHash == earlier.originalHash) {
                LogProviderCollision(provider->providerId, provider->providerId);
                ReleaseSRWLockExclusive(&g_providerLock);
                return FALSE;
            }
        }
        for (unsigned int providerIndex = 0;
             providerIndex < g_providerCount;
             ++providerIndex) {
            const auto& existingProvider = g_providers[providerIndex];
            for (unsigned int existingIndex = 0;
                 existingIndex < existingProvider.replacementCount;
                 ++existingIndex) {
                const auto& existing =
                    existingProvider.replacements[existingIndex];
                if (candidate.target == existing.target &&
                    candidate.originalHash == existing.originalHash) {
                    LogProviderCollision(
                        existingProvider.providerId, provider->providerId);
                    ReleaseSRWLockExclusive(&g_providerLock);
                    return FALSE;
                }
            }
        }
    }

    if (!StartHook()) {
        ReleaseSRWLockExclusive(&g_providerLock);
        return FALSE;
    }

    g_providers[g_providerCount++] = *provider;
    ReleaseSRWLockExclusive(&g_providerLock);
    return TRUE;
}
