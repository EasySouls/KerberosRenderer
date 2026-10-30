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

class AnimationClip : public Asset
{
public:
    AnimationClip(std::string name, const std::vector<AnimationTrack>& tracks, float duration);

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

private:
    std::string m_Name;
    std::vector<AnimationTrack> m_Tracks;
    float m_Duration;
};

}