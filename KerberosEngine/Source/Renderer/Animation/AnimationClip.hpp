#pragma once

#include "Assets/Asset.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

enum class AnimationPath : uint8_t
{
    Translation = 0,
    Rotation,
    Scale,
    // TODO: Weights
};

enum class AnimationInterpolation : uint8_t
{
    Linear = 0,
    Step,
    CubicSpline
};

struct AnimationTrack
{
    uint32_t Joint;
    AnimationPath Path;
    AnimationInterpolation Interpolation;

    std::vector<float> Times;
    std::vector<glm::vec4> Values;

    std::vector<glm::vec4> InTangents;
    std::vector<glm::vec4> OutTangents;
};

struct AnimationMarker
{
    float Time = 0.0f;
    std::string Name;
};

struct AnimationEvent
{
    float Time = 0.0f;
    float Duration = 0.0f; // 0 means instant event, > 0 means a duration event
    std::string Name;
    std::string Payload;
    bool FireInEditor = false;
    float MinTriggerWeight = 0.0f;
    uint32_t Flags = 0; // reserved for future use

    bool IsPointEvent() const
    {
        return Duration <= 0.0f;
    }
};

class AnimationClip : public Asset
{
public:
    AnimationClip(std::string name,
                  const std::vector<AnimationTrack>& tracks,
                  float duration,
                  std::vector<AnimationMarker> markers = {},
                  std::vector<AnimationEvent> events = {});

    static constexpr AssetType Type = AssetType::Animation;
    AssetType GetType() override
    {
        return Type;
    }

    const std::string& GetName() const
    {
        return m_Name;
    }

    const std::vector<AnimationTrack>& GetTracks() const
    {
        return m_Tracks;
    }

    float GetDuration() const
    {
        return m_Duration;
    }

    const std::vector<AnimationMarker>& GetMarkers() const;
    const std::vector<AnimationEvent>& GetEvents() const;

private:
    std::string m_Name;
    std::vector<AnimationTrack> m_Tracks;
    std::vector<AnimationMarker> m_Markers;
    std::vector<AnimationEvent> m_Events;
    float m_Duration;
};

}