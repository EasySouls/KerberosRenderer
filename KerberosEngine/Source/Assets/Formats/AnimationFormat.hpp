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

enum class AnimationClipInterpolation : uint8_t
{
    Linear,
    Step,
    CubicSpline
};

struct AnimationTrackFile
{
    uint32_t TargetNode = 0;                    // glTF node, kept for diagnostics/future
    int32_t JointIndex = -1;                    // final skeleton joint index
    AnimTrackPath Path;
    AnimationClipInterpolation Interpolation;
    std::vector<float> Times;
    std::vector<glm::vec4> Values;              // quat for rotation, vec3 packed in xyz for TRS
    std::vector<glm::vec4> InTangents;          // empty unless CubicSpline
    std::vector<glm::vec4> OutTangents;         // empty unless CubicSpline
};

struct AnimationClipFile
{
    uint32_t Version = 1;
    std::string LocalKey;       // "animation:<i>"
    uint32_t SourceIndex = 0;
    std::string Name;
    std::string SkeletonKey;    // "skeleton:<i>"
    uint32_t JointCount = 0;
    float DurationSeconds = 0.0f;
    std::vector<AnimationTrackFile> Tracks;
};

}
