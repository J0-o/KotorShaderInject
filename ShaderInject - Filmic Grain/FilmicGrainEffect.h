#pragma once

#include <windows.h>

namespace FilmicGrainEffect {

bool Initialize(HMODULE shaderInject);
bool Apply();

} // namespace FilmicGrainEffect
