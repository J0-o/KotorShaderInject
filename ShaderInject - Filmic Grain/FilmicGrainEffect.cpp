#include <windows.h>

#include "FilmicGrainEffect.h"
#include "../ShaderInject/ShaderInjectFullscreenProvider.h"

namespace {

ShaderInjectApplyFullscreenEffectFn g_applyFullscreenEffect = nullptr;

const char* kFragmentShader = R"GLSL(
#version 120
uniform sampler2D sceneTexture;
uniform float frameSeed;
varying vec2 vUv;

float hash(vec2 value) {
    vec3 value3 = fract(vec3(value.xyx) * 0.1031);
    value3 += dot(value3, value3.yzx + 33.33);
    return fract((value3.x + value3.y) * value3.z);
}

float gaussianNoise(vec2 pixel, vec2 offset) {
    float sum = hash(pixel + offset);
    sum += hash(pixel.yx + offset * 1.37 + vec2(17.0, 59.0));
    sum += hash(pixel + offset * 2.11 + vec2(101.0, 23.0));
    sum += hash(pixel.yx + offset * 0.73 + vec2(47.0, 131.0));
    return (sum - 2.0) * 0.7071;
}

float smoothNoise(vec2 value, vec2 offset) {
    vec2 cell = floor(value);
    vec2 blend = fract(value);
    blend = blend * blend * (3.0 - 2.0 * blend);
    float bottom = mix(hash(cell + offset),
                       hash(cell + vec2(1.0, 0.0) + offset), blend.x);
    float top = mix(hash(cell + vec2(0.0, 1.0) + offset),
                    hash(cell + vec2(1.0, 1.0) + offset), blend.x);
    return mix(bottom, top, blend.y);
}

void main() {
    vec4 scene = texture2D(sceneTexture, vUv);
    float luma = dot(scene.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec2 pixel = gl_FragCoord.xy;
    vec2 frameOffset = vec2(frameSeed * 19.19, frameSeed * 7.13);
    float fine = gaussianNoise(pixel, frameOffset);
    float clump = (smoothNoise(pixel * 0.5, frameOffset * 0.37) - 0.5) * 1.4142;
    float noise = (fine * 0.92 + clump * 0.20) * 1.06;
    float response = mix(0.85, 0.35, smoothstep(0.10, 0.95, luma));
    float grainedLuma = luma * exp2(noise * (0.21 * response));
    scene.rgb = clamp(scene.rgb * (grainedLuma / max(luma, 0.0001)), 0.0, 1.0);
    gl_FragColor = scene;
}
)GLSL";

const ShaderInjectFullscreenEffectV1 kEffect = {
    sizeof(ShaderInjectFullscreenEffectV1),
    SHADER_INJECT_FULLSCREEN_ABI_VERSION,
    "shader-inject-filmic-grain",
    kFragmentShader,
    "frameSeed",
};

} // namespace

namespace FilmicGrainEffect {

bool Initialize(HMODULE shaderInject) {
    g_applyFullscreenEffect =
        reinterpret_cast<ShaderInjectApplyFullscreenEffectFn>(
            GetProcAddress(shaderInject, "ShaderInject_ApplyFullscreenEffect"));
    return g_applyFullscreenEffect != nullptr;
}

bool Apply() {
    return g_applyFullscreenEffect && g_applyFullscreenEffect(&kEffect);
}

} // namespace FilmicGrainEffect
