#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

enum class AnimTrackPath : uint8_t
{
    Translation,
    Rotation,
    Scale
};

struct AnimationTrack
{
    uint32_t JointIndex;
    AnimTrackPath Path;
    std::vector<float> Times;
    std::vector<glm::vec4> Values; // quat for rotation, vec3 packed in xyz for TRS
};

struct AnimationClipFile
{
    std::string Name;
    float DurationSeconds = 0.0f;
    std::vector<AnimationTrack> Tracks;
};

}
