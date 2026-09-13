#include "../ShaderInjectSMAA.cpp"

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcA(window, message, wparam, lparam);
}

bool CompileValidationProgram(const char* wrapper, const char* label) {
    const char* vertexSources[] = {kVertexShader};
    const char* fragmentSources[] = {kSmaaPreamble, kSmaaReferenceSource, wrapper};
    GLuint vertex = g_glCreateShader(GL_VERTEX_SHADER_VALUE);
    g_glShaderSource(vertex, 1, vertexSources, nullptr);
    g_glCompileShader(vertex);
    GLint compiled = GL_FALSE;
    g_glGetShaderiv(vertex, GL_COMPILE_STATUS_VALUE, &compiled);
    if (compiled != GL_TRUE) {
        char log[8192] = {};
        g_glGetShaderInfoLog(vertex, sizeof(log), nullptr, log);
        std::printf("%s vertex compile failed:\n%s\n", label, log);
        return false;
    }

    GLuint fragment = g_glCreateShader(GL_FRAGMENT_SHADER_VALUE);
    g_glShaderSource(fragment, 3, fragmentSources, nullptr);
    g_glCompileShader(fragment);
    g_glGetShaderiv(fragment, GL_COMPILE_STATUS_VALUE, &compiled);
    if (compiled != GL_TRUE) {
        char log[8192] = {};
        g_glGetShaderInfoLog(fragment, sizeof(log), nullptr, log);
        std::printf("%s fragment compile failed:\n%s\n", label, log);
        return false;
    }

    GLuint program = g_glCreateProgram();
    g_glAttachShader(program, vertex);
    g_glAttachShader(program, fragment);
    g_glLinkProgram(program);
    GLint linked = GL_FALSE;
    g_glGetProgramiv(program, GL_LINK_STATUS_VALUE, &linked);
    if (linked != GL_TRUE) {
        char log[8192] = {};
        g_glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::printf("%s link failed:\n%s\n", label, log);
        return false;
    }
    std::printf("%s: OK\n", label);
    return true;
}

} // namespace

int main() {
    WNDCLASSA windowClass = {};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = "ShaderInjectSmaaValidation";
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
    format.cDepthBits = 24;
    const int pixelFormat = ChoosePixelFormat(device, &format);
    if (!pixelFormat || !SetPixelFormat(device, pixelFormat, &format)) return 3;
    HGLRC context = wglCreateContext(device);
    if (!context || !wglMakeCurrent(device, context) || !LoadFunctions()) return 4;

    const bool valid =
        CompileValidationProgram(kEdgeWrapper, "edge") &&
        CompileValidationProgram(kBlendWrapper, "blend") &&
        CompileValidationProgram(kNeighborhoodWrapper, "neighborhood");
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    ReleaseDC(window, device);
    DestroyWindow(window);
    return valid ? 0 : 5;
}

