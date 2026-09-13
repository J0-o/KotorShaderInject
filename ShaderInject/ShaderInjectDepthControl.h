#pragma once

#include <windows.h>

using ShaderInjectBeginDepthWriteSuppressionFn = BOOL (__cdecl*)();
using ShaderInjectEndDepthWriteSuppressionFn = void (__cdecl*)();
