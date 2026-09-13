#include <windows.h>

#include "DepthOfFieldEffect.h"
#include "DialogueTracker.h"
#include "../ShaderInject/ShaderInjectProvider.h"

namespace {

void __cdecl OnFinalScene(const ShaderInjectFrameContext* frame) {
    ShaderInjectDof::DialogueFocusTarget target = {};
    if (!ShaderInjectDof::FindDialogueFocusTarget(frame, target)) {
        ShaderInjectDof::ResetDepthOfField();
        return;
    }
    ShaderInjectDof::ApplyDepthOfField(frame, target);
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
        SHADER_INJECT_PRIORITY_DEPTH_OF_FIELD,
    };
    if (!registerProvider || !registerProvider(&provider)) {
        InterlockedExchange(&initialized, 0);
        return FALSE;
    }
    OutputDebugStringA("[DOF] ShaderInject cinematic depth-of-field provider loaded.\n");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        return KPatch_Initialize();
    }
    return TRUE;
}
