#include <windows.h>
#include <GL/gl.h>

#include <cstddef>
#include <cstring>

#include "ShaderInjectProvider.h"

#pragma comment(lib, "opengl32.lib")

namespace {

struct CameraLayout {
    size_t scene;
    size_t fovY;
    size_t nearPlane;
    size_t farPlane;
    size_t viewportX;
    size_t viewportY;
    size_t viewportWidth;
    size_t viewportHeight;
};

constexpr CameraLayout kKotor1Camera = {
    0x6C, 0x1D0, 0x1DC, 0x1E0, 0x1E4, 0x1E8, 0x1EC, 0x1F0,
};
constexpr CameraLayout kKotor2Camera = {
    0x98, 0x204, 0x210, 0x214, 0x218, 0x21C, 0x220, 0x224,
};
constexpr unsigned int kMaxProviders = 16;

ShaderInjectProviderV1 g_providers[kMaxProviders]{};
unsigned int g_providerCount = 0;
SRWLOCK g_providerLock = SRWLOCK_INIT;
ShaderInjectFrameContext g_frame{};

void DispatchFrame(
    void (__cdecl* ShaderInjectProviderV1::*callback)(const ShaderInjectFrameContext*)) {
    ShaderInjectProviderV1 providers[kMaxProviders]{};
    AcquireSRWLockShared(&g_providerLock);
    const unsigned int providerCount = g_providerCount;
    std::memcpy(providers, g_providers,
                providerCount * sizeof(ShaderInjectProviderV1));
    ReleaseSRWLockShared(&g_providerLock);
    for (unsigned int index = 0; index < providerCount; ++index) {
        auto function = providers[index].*callback;
        if (!function) continue;
        __try {
            function(&g_frame);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            OutputDebugStringA("[ShaderInject] Provider frame callback raised an exception.\n");
        }
    }
}

void DispatchMeshBegin(void* node) {
    ShaderInjectProviderV1 providers[kMaxProviders]{};
    AcquireSRWLockShared(&g_providerLock);
    const unsigned int providerCount = g_providerCount;
    std::memcpy(providers, g_providers,
                providerCount * sizeof(ShaderInjectProviderV1));
    ReleaseSRWLockShared(&g_providerLock);
    for (unsigned int index = 0; index < providerCount; ++index) {
        auto function = providers[index].onMeshBegin;
        if (!function) continue;
        __try {
            function(node);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            OutputDebugStringA("[ShaderInject] Provider mesh-begin callback raised an exception.\n");
        }
    }
}

void DispatchMeshEnd() {
    ShaderInjectProviderV1 providers[kMaxProviders]{};
    AcquireSRWLockShared(&g_providerLock);
    const unsigned int providerCount = g_providerCount;
    std::memcpy(providers, g_providers,
                providerCount * sizeof(ShaderInjectProviderV1));
    ReleaseSRWLockShared(&g_providerLock);
    for (unsigned int index = providerCount; index > 0; --index) {
        auto function = providers[index - 1].onMeshEnd;
        if (!function) continue;
        __try {
            function();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            OutputDebugStringA("[ShaderInject] Provider mesh-end callback raised an exception.\n");
        }
    }
}

void ResetFrame() {
    std::memset(&g_frame, 0, sizeof(g_frame));
    g_frame.structSize = sizeof(g_frame);
}

void CacheCamera(
    void* camera, const CameraLayout& layout, ShaderInjectGame game) {
    ResetFrame();
    if (!camera) return;

    __try {
        const auto* bytes = static_cast<const unsigned char*>(camera);
        g_frame.camera = camera;
        g_frame.scene = *reinterpret_cast<void* const*>(bytes + layout.scene);
        g_frame.game = game;
        g_frame.fovY = *reinterpret_cast<const float*>(bytes + layout.fovY);
        g_frame.nearPlane =
            *reinterpret_cast<const float*>(bytes + layout.nearPlane);
        g_frame.farPlane =
            *reinterpret_cast<const float*>(bytes + layout.farPlane);
        g_frame.viewportX =
            *reinterpret_cast<const int*>(bytes + layout.viewportX);
        g_frame.viewportY =
            *reinterpret_cast<const int*>(bytes + layout.viewportY);
        g_frame.viewportWidth =
            *reinterpret_cast<const int*>(bytes + layout.viewportWidth);
        g_frame.viewportHeight =
            *reinterpret_cast<const int*>(bytes + layout.viewportHeight);
        g_frame.cameraDataValid = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFrame();
        return;
    }
    if (!g_frame.scene) {
        ResetFrame();
        return;
    }

    if (wglGetCurrentContext()) {
        glGetFloatv(GL_MODELVIEW_MATRIX, g_frame.modelView);
        glGetFloatv(GL_PROJECTION_MATRIX, g_frame.projection);
        g_frame.matricesValid = TRUE;
    }
    DispatchFrame(&ShaderInjectProviderV1::onFrameBegin);
}

} // namespace

extern "C" BOOL __cdecl ShaderInject_RegisterProvider(
    const ShaderInjectProviderV1* provider) {
    if (!provider ||
        provider->structSize < SHADER_INJECT_PROVIDER_V1_BASE_SIZE ||
        provider->abiVersion != SHADER_INJECT_ABI_VERSION) {
        return FALSE;
    }

    AcquireSRWLockExclusive(&g_providerLock);
    if (g_providerCount >= kMaxProviders) {
        ReleaseSRWLockExclusive(&g_providerLock);
        return FALSE;
    }
    ShaderInjectProviderV1 copy = {};
    std::memcpy(&copy, provider,
                provider->structSize < sizeof(copy) ? provider->structSize
                                                    : sizeof(copy));
    unsigned int insertion = g_providerCount;
    while (insertion > 0 &&
           g_providers[insertion - 1].priority > copy.priority) {
        g_providers[insertion] = g_providers[insertion - 1];
        --insertion;
    }
    g_providers[insertion] = copy;
    ++g_providerCount;
    ReleaseSRWLockExclusive(&g_providerLock);
    return TRUE;
}

extern "C" void __cdecl ShaderInject_CacheCamera(void* camera) {
    CacheCamera(camera, kKotor1Camera, SHADER_INJECT_GAME_KOTOR1);
}

extern "C" void __cdecl ShaderInject_CacheCameraKotor2(void* camera) {
    CacheCamera(camera, kKotor2Camera, SHADER_INJECT_GAME_KOTOR2);
}

extern "C" void __cdecl ShaderInject_BeforeParticles(void* scene) {
    if (!scene || scene != g_frame.scene) return;
    DispatchFrame(&ShaderInjectProviderV1::onBeforeParticles);
}

extern "C" void __cdecl ShaderInject_FinalScene(void* camera) {
    if (!camera || camera != g_frame.camera) return;
    DispatchFrame(&ShaderInjectProviderV1::onFinalScene);
    ResetFrame();
}

extern "C" void __cdecl ShaderInject_FinalSceneCached() {
    if (!g_frame.camera) return;
    DispatchFrame(&ShaderInjectProviderV1::onFinalScene);
    ResetFrame();
}

extern "C" void __cdecl ShaderInject_BeginMesh(void* node) {
    DispatchMeshBegin(node);
}

extern "C" void __cdecl ShaderInject_BeginMeshIndirect(void* const* nodeSlot) {
    if (!nodeSlot) return;
    __try {
        DispatchMeshBegin(*nodeSlot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[ShaderInject] Could not read the mesh hook argument.\n");
    }
}

extern "C" void __cdecl ShaderInject_EndMesh() {
    DispatchMeshEnd();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ResetFrame();
        OutputDebugStringA("[ShaderInject] Shared render injection framework loaded.\n");
    }
    return TRUE;
}
