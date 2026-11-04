#include "AnimationClip.hpp"

#include <algorithm>
#include <cmath>

import Kerberos;

namespace Kerberos {

AnimationClip::AnimationClip(std::string name,
    const std::vector<AnimationTrack>& tracks,
    const float duration,
    std::vector<AnimationMarker> markers,
    std::vector<AnimationEvent> events)
    : m_Name(std::move(name)), m_Tracks(tracks), m_Markers(std::move(markers)),
      m_Events(std::move(events)), m_Duration(duration)
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

    // Markers/events come from authoring metadata, so sanitize instead of asserting. Invalid
    // entries are dropped. Times are clamped to the clip range and sorted for runtime queries.
    std::erase_if(m_Markers, [](const AnimationMarker& marker)
    {
        return !std::isfinite(marker.Time);
    });
    std::ranges::stable_sort(m_Markers, {}, &AnimationMarker::Time);

    std::erase_if(m_Events, [](const AnimationEvent& event)
    {
        return !std::isfinite(event.Time) || !std::isfinite(event.Duration) ||
            !std::isfinite(event.MinTriggerWeight);
    });
    std::ranges::stable_sort(m_Events, {}, &AnimationEvent::Time);

    if (m_Duration > 0.0f)
    {
        for (AnimationMarker& marker : m_Markers)
            marker.Time = std::clamp(marker.Time, 0.0f, m_Duration);

        for (AnimationEvent& event : m_Events)
        {
            event.Time = std::clamp(event.Time, 0.0f, m_Duration);
            event.Duration = std::max(0.0f, event.Duration);
        }
    }
    else
    {
        for (AnimationMarker& marker : m_Markers)
            marker.Time = 0.0f;

        for (AnimationEvent& event : m_Events)
        {
            event.Time = 0.0f;
            event.Duration = 0.0f;
        }
    }
}

const std::vector<AnimationMarker>& AnimationClip::GetMarkers() const
{
    return m_Markers;
}

const std::vector<AnimationEvent>& AnimationClip::GetEvents() const
{
    return m_Events;
}

}