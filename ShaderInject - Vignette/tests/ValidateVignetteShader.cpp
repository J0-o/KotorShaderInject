#include <windows.h>
#include <GL/gl.h>

#include "../VignetteEffect.cpp"

#include <cstdio>

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcA(window, message, wparam, lparam);
}

} // namespace

int main() {
    WNDCLASSA windowClass = {};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = "ShaderInjectVignetteValidation";
    if (!RegisterClassA(&windowClass)) return 1;
    HWND window = CreateWindowA(windowClass.lpszClassName, "", WS_POPUP,
                                0, 0, 16, 16, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
    if (!window) return 2;
    HDC device = GetDC(window);
    PIXELFORMATDESCRIPTOR format = {};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    const int pixelFormat = ChoosePixelFormat(device, &format);
    if (!pixelFormat || !SetPixelFormat(device, pixelFormat, &format)) return 3;
    HGLRC context = wglCreateContext(device);
    if (!context || !wglMakeCurrent(device, context)) return 4;
    HMODULE shaderInject = LoadLibraryA("..\\..\\ShaderInject\\windows_x86.dll");
    if (!shaderInject || !VignetteEffect::Initialize(shaderInject)) return 5;
    glViewport(0, 0, 16, 16);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const bool applied = VignetteEffect::Apply();
    unsigned char center[4] = {};
    unsigned char corner[4] = {};
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    const bool valid = applied && center[0] >= 250 &&
                       corner[0] >= 215 && corner[0] <= 235 &&
                       corner[0] < center[0];
    std::printf("vignette: %s (center=%u corner=%u)\n",
                valid ? "OK" : "FAILED", center[0], corner[0]);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    ReleaseDC(window, device);
    DestroyWindow(window);
    FreeLibrary(shaderInject);
    return valid ? 0 : 6;
}
