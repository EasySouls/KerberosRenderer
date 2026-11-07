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

struct AnimationMarkerFile
{
    float Time = 0.0f;
    std::string Name;
};

enum class AnimationEventParameterTypeFile : uint8_t
{
    Bool = 0,
    Int,
    Float,
    Vector3,
    String,
    Asset
};

struct AnimationEventParameterFile
{
    std::string Name;
    AnimationEventParameterTypeFile Type = AnimationEventParameterTypeFile::Float;
    bool BoolValue = false;
    int32_t IntValue = 0;
    float FloatValue = 0.0f;
    glm::vec3 VectorValue = glm::vec3(0.0f);
    std::string StringValue;
    uint64_t AssetValue = 0;
};

struct AnimationEventFile
{
    float Time = 0.0f;
    float Duration = 0.0f; // 0 means instant event, > 0 means a duration event
    std::string Name;
    std::string Payload;
    bool FireInEditor = false;
    float MinTriggerWeight = 0.0f;
    uint32_t Flags = 0; // reserved for future use
    std::vector<AnimationEventParameterFile> Parameters;
};

struct AnimationClipFile
{
    uint32_t Version = 3;
    std::string LocalKey;       // "animation:<i>"
    uint32_t SourceIndex = 0;
    std::string Name;
    std::string SkeletonKey;    // "skeleton:<i>"
    uint32_t JointCount = 0;
    float DurationSeconds = 0.0f;
    std::vector<AnimationTrackFile> Tracks;
    std::vector<AnimationMarkerFile> Markers;
    std::vector<AnimationEventFile> Events;
};

}
