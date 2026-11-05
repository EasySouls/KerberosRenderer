#pragma once

#include "Assets/Asset.hpp"

#include <cstdint>

namespace Kerberos {

class AnimationClip;
struct AnimationEvent;

enum class AnimationEventPhase : uint8_t
{
    Point = 0,
    Begin,
    Tick,
    End
};

/// One runtime event dispatch produced by the CPU animation runtime and consumed by the scene.
struct DispatchedAnimationEvent
{
    UUID Entity = UUID::Invalid();
    const AnimationClip* Clip = nullptr;
    const AnimationEvent* Event = nullptr;
    int32_t EventIndex = -1;
    AnimationEventPhase Phase = AnimationEventPhase::Point;
    float Weight = 1.0f;
    float Time = 0.0f;
};

}
