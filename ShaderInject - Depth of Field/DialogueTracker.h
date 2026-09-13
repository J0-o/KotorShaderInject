#pragma once

#include "../ShaderInject/ShaderInjectProvider.h"

namespace ShaderInjectDof {

struct DialogueFocusTarget {
    float distance;
    float screenX;
    float screenY;
};

bool FindDialogueFocusTarget(const ShaderInjectFrameContext* frame,
                             DialogueFocusTarget& target);

} // namespace ShaderInjectDof
