#include <windows.h>

#include "VignetteEffect.h"
#include "../ShaderInject/ShaderInjectFullscreenProvider.h"

namespace {

ShaderInjectApplyFullscreenEffectFn g_applyFullscreenEffect = nullptr;

const char* kFragmentShader = R"GLSL(
#version 120
uniform sampler2D sceneTexture;
varying vec2 vUv;

void main() {
    vec4 scene = texture2D(sceneTexture, vUv);
    vec2 centered = vUv * 2.0 - 1.0;
    float radiusSquared = dot(centered, centered);
    float edge = smoothstep(0.55, 1.50, radiusSquared);
    scene.rgb *= 1.0 - 0.12 * edge;
    gl_FragColor = scene;
}
)GLSL";

const ShaderInjectFullscreenEffectV1 kEffect = {
    sizeof(ShaderInjectFullscreenEffectV1),
    SHADER_INJECT_FULLSCREEN_ABI_VERSION,
    "shader-inject-vignette",
    kFragmentShader,
    nullptr,
};

} // namespace

namespace VignetteEffect {

bool Initialize(HMODULE shaderInject) {
    g_applyFullscreenEffect =
        reinterpret_cast<ShaderInjectApplyFullscreenEffectFn>(
            GetProcAddress(shaderInject, "ShaderInject_ApplyFullscreenEffect"));
    return g_applyFullscreenEffect != nullptr;
}

bool Apply() {
    return g_applyFullscreenEffect && g_applyFullscreenEffect(&kEffect);
}

} // namespace VignetteEffect
