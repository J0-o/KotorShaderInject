#pragma once

#include "DialogueTracker.h"

namespace ShaderInjectDof {

void ResetDepthOfField();
void ApplyDepthOfField(const ShaderInjectFrameContext* frame,
                       const DialogueFocusTarget& focusTarget);

} // namespace ShaderInjectDof
