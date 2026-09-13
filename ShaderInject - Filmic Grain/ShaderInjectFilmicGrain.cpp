#include <windows.h>

#include <cstdio>

#include "FilmicGrainEffect.h"
#include "../ShaderInject/ShaderInjectProvider.h"

namespace {

bool g_failureLogged = false;
bool g_firstFrameLogged = false;

void Log(const char* message) {
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

int HandleException(EXCEPTION_POINTERS* exception) {
    char message[192] = {};
    sprintf_s(message, "[FilmicGrain] Exception 0x%08lX at %p.",
              exception->ExceptionRecord->ExceptionCode,
              exception->ExceptionRecord->ExceptionAddress);
    Log(message);
    return EXCEPTION_EXECUTE_HANDLER;
}

void __cdecl OnFinalScene(const ShaderInjectFrameContext* frame) {
    if (!ShaderInject_IsMainPerspectiveCamera(frame)) return;
    __try {
        if (!FilmicGrainEffect::Apply()) {
            if (!g_failureLogged) {
                g_failureLogged = true;
                Log("[FilmicGrain] OpenGL shader resources are unavailable.");
            }
            return;
        }
        if (!g_firstFrameLogged) {
            g_firstFrameLogged = true;
            Log("[FilmicGrain] First final-scene grain pass completed.");
        }
    }
    __except (HandleException(GetExceptionInformation())) {
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
    if (!FilmicGrainEffect::Initialize(shaderInject)) {
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
        SHADER_INJECT_PRIORITY_FILM_GRAIN,
    };
    if (!registerProvider || !registerProvider(&provider)) {
        InterlockedExchange(&initialized, 0);
        return FALSE;
    }
    Log("[FilmicGrain] ShaderInject filmic-grain provider loaded.");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        return KPatch_Initialize();
    }
    return TRUE;
}
