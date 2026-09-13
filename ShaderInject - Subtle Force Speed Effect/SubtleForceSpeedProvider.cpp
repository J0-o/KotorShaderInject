#include <windows.h>

#include "../ShaderInject/ShaderInjectReplacementProvider.h"

#include "ShaderReplacements.generated.h"

namespace {
    LONG g_registered = 0;
}

extern "C" BOOL __cdecl KPatch_Initialize() {
    if (InterlockedCompareExchange(&g_registered, 1, 0) != 0) {
        return TRUE;
    }

    HMODULE shaderInject = GetModuleHandleA("shader-inject.dll");
    if (!shaderInject) {
        InterlockedExchange(&g_registered, 0);
        return FALSE;
    }

    auto registerProvider =
        reinterpret_cast<ShaderInjectRegisterReplacementProviderFn>(
            GetProcAddress(
                shaderInject, "ShaderInject_RegisterReplacementProvider"));
    const ShaderInjectReplacementProviderV1 provider = {
        sizeof(ShaderInjectReplacementProviderV1),
        SHADER_INJECT_REPLACEMENT_ABI_VERSION,
        "shader-inject-subtle-force-speed-effect",
        kShaderReplacements,
        kShaderReplacementCount,
    };
    if (!registerProvider || !registerProvider(&provider)) {
        InterlockedExchange(&g_registered, 0);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        return KPatch_Initialize();
    }
    return TRUE;
}
