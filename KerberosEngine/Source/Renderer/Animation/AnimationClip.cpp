#include "AnimationClip.hpp"

#include <algorithm>

import Kerberos;

namespace Kerberos {

AnimationClip::AnimationClip(std::string name, const std::vector<AnimationTrack>& tracks, const float duration)
    : m_Name(std::move(name)), m_Tracks(tracks), m_Duration(duration)
{
    for (const auto& track : m_Tracks)
    {
        // Ensure that the times are sorted
        KBRAssert(std::ranges::is_sorted(track.Times), "Animation times are not sorted");

        // Ensure that every track has the same number of times and values
        KBRAssert(track.Times.size() == track.Values.size(), "Animation times and values are not the same size");
        
        // Validate that the duration is greater than or equal to the last time in each track
        if (!track.Times.empty())
        {
            KBRAssert(m_Duration >= track.Times.back(), "Animation duration is less than the last time in a track");
        
        }
        // Validate that the joint indices are valid
        KBRAssert(track.Joint < 65535, "Animation joint index is out of bounds");

        // If using CubicSpline interpolation, ensure that the in and out tangents are the same size as the values
        if (track.Interpolation == AnimationInterpolation::CubicSpline)
        {
            KBRAssert(track.InTangents.size() == track.Values.size(),
                      "Animation in tangents and values are not the same size");
            KBRAssert(track.OutTangents.size() == track.Values.size(),
                      "Animation out tangents and values are not the same size");
        }
    }
}

}