#include <windows.h>
#include <GL/gl.h>

#include <cstring>

#include "ShaderInjectDepthControl.h"

#pragma comment(lib, "opengl32.lib")

namespace {

using GlDepthMaskFn = void (APIENTRY*)(GLboolean);

GlDepthMaskFn g_originalDepthMask = nullptr;
bool g_suppressDepthWrites = false;
GLboolean g_savedDepthMask = GL_TRUE;

GlDepthMaskFn* FindDepthMaskImportSlot() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return nullptr;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const auto& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return nullptr;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->OriginalFirstThunk || !descriptor->FirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + names->u1.AddressOfData);
            if (std::strcmp(
                    reinterpret_cast<const char*>(import->Name),
                    "glDepthMask") == 0) {
                return reinterpret_cast<GlDepthMaskFn*>(&slots->u1.Function);
            }
        }
    }
    return nullptr;
}

void APIENTRY HookedDepthMask(GLboolean enabled) {
    g_originalDepthMask(g_suppressDepthWrites ? GL_FALSE : enabled);
}

bool EnsureDepthMaskHook() {
    if (g_originalDepthMask) return true;
    auto* slot = FindDepthMaskImportSlot();
    if (!slot || !*slot) return false;
    DWORD oldProtection = 0;
    if (!VirtualProtect(
            slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) {
        return false;
    }
    g_originalDepthMask = *slot;
    *slot = &HookedDepthMask;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return true;
}

} // namespace

extern "C" BOOL __cdecl ShaderInject_BeginDepthWriteSuppression() {
    if (g_suppressDepthWrites || !EnsureDepthMaskHook()) return FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &g_savedDepthMask);
    g_suppressDepthWrites = true;
    g_originalDepthMask(GL_FALSE);
    return TRUE;
}

extern "C" void __cdecl ShaderInject_EndDepthWriteSuppression() {
    if (!g_suppressDepthWrites || !g_originalDepthMask) return;
    g_suppressDepthWrites = false;
    g_originalDepthMask(g_savedDepthMask);
}
