#include "DialogueTracker.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr uintptr_t kAppManagerRva = 0x003A39FC;
constexpr uintptr_t kGetGameObjectRva = 0x001ED580;
constexpr size_t kAppManagerClientOffset = 0x04;
constexpr size_t kClientInternalOffset = 0x04;
constexpr size_t kInternalGuiOffset = 0x40;
constexpr size_t kKotor1CameraBehaviorOffset = 0x18C;
constexpr size_t kKotor2CameraBehaviorOffset = 0x1B8;
constexpr size_t kObjectPositionOffset = 0x24;
constexpr size_t kGuiDialogPanelOffset = 0x3C;
constexpr size_t kGuiCinematicPanelOffset = 0x40;
constexpr size_t kGuiGlobalDialogStateOffset = 0xB4;
constexpr size_t kGuiParticipantAOffset = 0x170;
constexpr size_t kGuiParticipantBOffset = 0x174;
constexpr size_t kGuiRootListenerOffset = 0x184;
constexpr size_t kGuiAnimationParticipantCountOffset = 0x198;
constexpr size_t kGuiAnimationParticipantIdsOffset = 0x1A0;
constexpr size_t kBehaviorHeightAOffset = 0x14;
constexpr size_t kBehaviorHeightBOffset = 0x18;
constexpr size_t kBehaviorParticipantAOffset = 0x5C;
constexpr size_t kBehaviorParticipantBOffset = 0x60;
constexpr size_t kBehaviorParticipantObjectAOffset = 0x64;
constexpr size_t kBehaviorParticipantObjectBOffset = 0x68;
constexpr size_t kGetBehaviorTypeVtableOffset = 0x10;
constexpr size_t kGetObjectPositionVtableOffset = 0x64;
constexpr int kDialogBehaviorType = 0x106D;
constexpr std::uint32_t kInvalidObjectId = 0x7F000000u;
constexpr int kMaxDialogueParticipants = 32;

using GetGameObjectFn = void* (__thiscall*)(void*, std::uint32_t);
using GetBehaviorTypeFn = int (__thiscall*)(void*);

struct Vec3 {
    float x;
    float y;
    float z;
};

using GetObjectPositionFn = Vec3* (__thiscall*)(void*, Vec3*);

struct DialogueState {
    void* client;
    void* gui;
    std::uint32_t participants[kMaxDialogueParticipants];
    int participantCount;
};

struct DialogBehaviorState {
    std::uint32_t participantA;
    std::uint32_t participantB;
    float heightA;
    float heightB;
    void* objectA;
    void* objectB;
};

bool ReadDialogueState(DialogueState& state) {
    std::memset(&state, 0, sizeof(state));
    __try {
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        if (!base) return false;
        void* appManager = *reinterpret_cast<void**>(base + kAppManagerRva);
        if (!appManager) return false;
        auto* appBytes = static_cast<unsigned char*>(appManager);
        state.client = *reinterpret_cast<void**>(
            appBytes + kAppManagerClientOffset);
        if (!state.client) return false;
        auto* clientBytes = static_cast<unsigned char*>(state.client);
        void* internal = *reinterpret_cast<void**>(
            clientBytes + kClientInternalOffset);
        if (!internal) return false;
        state.gui = *reinterpret_cast<void**>(
            static_cast<unsigned char*>(internal) + kInternalGuiOffset);
        if (!state.gui) return false;
        auto* guiBytes = static_cast<unsigned char*>(state.gui);
        if (*reinterpret_cast<int*>(guiBytes + kGuiGlobalDialogStateOffset) == 0) {
            return false;
        }
        void* dialogPanel = *reinterpret_cast<void**>(guiBytes + kGuiDialogPanelOffset);
        void* cinematicPanel = *reinterpret_cast<void**>(guiBytes + kGuiCinematicPanelOffset);
        if (!dialogPanel || dialogPanel != cinematicPanel) return false;
        const auto addParticipant = [&state](std::uint32_t id) {
            if (!id || id == kInvalidObjectId) return;
            for (int i = 0; i < state.participantCount; ++i) {
                if (state.participants[i] == id) return;
            }
            if (state.participantCount < kMaxDialogueParticipants) {
                state.participants[state.participantCount++] = id;
            }
        };
        addParticipant(*reinterpret_cast<std::uint32_t*>(
            guiBytes + kGuiParticipantAOffset));
        addParticipant(*reinterpret_cast<std::uint32_t*>(
            guiBytes + kGuiParticipantBOffset));
        addParticipant(*reinterpret_cast<std::uint32_t*>(
            guiBytes + kGuiRootListenerOffset));
        const int animationParticipantCount = *reinterpret_cast<int*>(
            guiBytes + kGuiAnimationParticipantCountOffset);
        auto* animationParticipants = *reinterpret_cast<std::uint32_t**>(
            guiBytes + kGuiAnimationParticipantIdsOffset);
        if (animationParticipantCount > 0 && animationParticipants) {
            const int boundedCount = (std::min)(animationParticipantCount,
                                                kMaxDialogueParticipants);
            for (int i = 0; i < boundedCount; ++i) {
                addParticipant(animationParticipants[i]);
            }
        }
        return state.participantCount > 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadDialogBehavior(const ShaderInjectFrameContext* frame,
                        DialogBehaviorState& state) {
    state = {0, 0, 1.5f, 1.5f, nullptr, nullptr};
    if (!frame ||
        frame->structSize < SHADER_INJECT_FRAME_CONTEXT_CAMERA_SIZE) {
        return false;
    }
    __try {
        auto* cameraBytes = static_cast<unsigned char*>(frame->camera);
        const size_t behaviorOffset =
            frame->game == SHADER_INJECT_GAME_KOTOR2
                ? kKotor2CameraBehaviorOffset
                : kKotor1CameraBehaviorOffset;
        void* behavior = *reinterpret_cast<void**>(cameraBytes + behaviorOffset);
        if (!behavior) return false;
        void** vtable = *reinterpret_cast<void***>(behavior);
        if (!vtable) return false;
        auto getType = reinterpret_cast<GetBehaviorTypeFn>(
            *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(vtable) +
                kGetBehaviorTypeVtableOffset));
        if (!getType || getType(behavior) != kDialogBehaviorType) return false;
        auto* bytes = static_cast<unsigned char*>(behavior);
        state.participantA =
            *reinterpret_cast<std::uint32_t*>(bytes + kBehaviorParticipantAOffset);
        state.participantB =
            *reinterpret_cast<std::uint32_t*>(bytes + kBehaviorParticipantBOffset);
        state.objectA = *reinterpret_cast<void**>(
            bytes + kBehaviorParticipantObjectAOffset);
        state.objectB = *reinterpret_cast<void**>(
            bytes + kBehaviorParticipantObjectBOffset);
        const float valueA = *reinterpret_cast<float*>(bytes + kBehaviorHeightAOffset);
        const float valueB = *reinterpret_cast<float*>(bytes + kBehaviorHeightBOffset);
        if (valueA > 0.5f && valueA < 3.5f) state.heightA = valueA;
        if (valueB > 0.5f && valueB < 3.5f) state.heightB = valueB;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float ParticipantHeight(const DialogBehaviorState& behavior, std::uint32_t id) {
    if (behavior.participantA == id) return behavior.heightA;
    if (behavior.participantB == id) return behavior.heightB;
    return 1.5f;
}

bool ProjectCandidate(const ShaderInjectFrameContext* frame, const Vec3& world,
                      ShaderInjectDof::DialogueFocusTarget& candidate) {
    const float* m = frame->modelView;
    const float eye[4] = {
        m[0] * world.x + m[4] * world.y + m[8]  * world.z + m[12],
        m[1] * world.x + m[5] * world.y + m[9]  * world.z + m[13],
        m[2] * world.x + m[6] * world.y + m[10] * world.z + m[14],
        m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15],
    };
    const float* p = frame->projection;
    const float clipX = p[0] * eye[0] + p[4] * eye[1] + p[8]  * eye[2] + p[12] * eye[3];
    const float clipY = p[1] * eye[0] + p[5] * eye[1] + p[9]  * eye[2] + p[13] * eye[3];
    const float clipW = p[3] * eye[0] + p[7] * eye[1] + p[11] * eye[2] + p[15] * eye[3];
    const float distance = -eye[2];
    if (distance <= 0.05f || clipW <= 0.0001f) return false;
    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;
    if (std::fabs(ndcX) > 1.15f || std::fabs(ndcY) > 1.15f) return false;
    candidate = {distance, ndcX * 0.5f + 0.5f, ndcY * 0.5f + 0.5f};
    return true;
}

bool ResolveBehaviorObject(const ShaderInjectFrameContext* frame, void* object,
                           float height,
                           ShaderInjectDof::DialogueFocusTarget& candidate) {
    if (!object) return false;
    __try {
        void** vtable = *reinterpret_cast<void***>(object);
        if (!vtable) return false;
        auto getPosition = reinterpret_cast<GetObjectPositionFn>(
            *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(vtable) +
                kGetObjectPositionVtableOffset));
        if (!getPosition) return false;
        Vec3 world = {};
        getPosition(object, &world);
        world.z += height;
        return ProjectCandidate(frame, world, candidate);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ResolveCandidate(const ShaderInjectFrameContext* frame,
                      const DialogueState& state, std::uint32_t id,
                      float height, ShaderInjectDof::DialogueFocusTarget& candidate) {
    if (!id || id == kInvalidObjectId) return false;
    __try {
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        auto getObject = reinterpret_cast<GetGameObjectFn>(base + kGetGameObjectRva);
        void* object = getObject(state.client, id);
        if (!object) return false;
        Vec3 world = *reinterpret_cast<Vec3*>(
            static_cast<unsigned char*>(object) + kObjectPositionOffset);
        world.z += height;
        return ProjectCandidate(frame, world, candidate);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

namespace ShaderInjectDof {

bool FindDialogueFocusTarget(const ShaderInjectFrameContext* frame,
                             DialogueFocusTarget& target) {
    if (!frame || !frame->matricesValid ||
        frame->structSize < SHADER_INJECT_FRAME_CONTEXT_CAMERA_SIZE) return false;
    DialogBehaviorState behavior = {};
    if (!ReadDialogBehavior(frame, behavior)) return false;
    if (frame->game == SHADER_INJECT_GAME_KOTOR2) {
        bool foundParticipant = false;
        void* objects[2] = {behavior.objectA, behavior.objectB};
        const float heights[2] = {behavior.heightA, behavior.heightB};
        for (int i = 0; i < 2; ++i) {
            DialogueFocusTarget candidate = {};
            if (!ResolveBehaviorObject(frame, objects[i], heights[i], candidate))
                continue;
            if (!foundParticipant || candidate.distance > target.distance) {
                target = candidate;
                foundParticipant = true;
            }
        }
        return foundParticipant;
    }
    DialogueState dialogue = {};
    if (!ReadDialogueState(dialogue)) return false;
    bool foundParticipant = false;
    for (int i = 0; i < dialogue.participantCount; ++i) {
        DialogueFocusTarget candidate = {};
        const std::uint32_t id = dialogue.participants[i];
        if (!ResolveCandidate(frame, dialogue, id, ParticipantHeight(behavior, id),
                              candidate)) {
            continue;
        }
        if (!foundParticipant || candidate.distance > target.distance) {
            target = candidate;
            foundParticipant = true;
        }
    }
    return foundParticipant;
}

} // namespace ShaderInjectDof
