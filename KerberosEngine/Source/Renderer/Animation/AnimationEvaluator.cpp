#include "AnimationEvaluator.hpp"

#include "Profiling/TracyInstrumentation.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

import Kerberos;

namespace Kerberos {

namespace {

glm::quat ToQuat(const glm::vec4& value)
{
    return { value.w, value.x, value.y, value.z };
}

glm::vec4 ToVec4(const glm::quat& value)
{
    return { value.x, value.y, value.z, value.w };
}

glm::quat NormalizeOrIdentity(const glm::quat& value)
{
    const float lengthSquared = glm::dot(value, value);
    if (lengthSquared <= 1.0e-12f)
        return { 1.0f, 0.0f, 0.0f, 0.0f };

    return value / std::sqrt(lengthSquared);
}

}

ClipTrackCache BuildClipTrackCache(const Skeleton& skeleton, const AnimationClip& clip)
{
    KBR_TRACY_FUNCTION();

    ClipTrackCache cache{};
    cache.JointCount = static_cast<uint32_t>(skeleton.GetJoints().size());
    cache.TracksByJoint.assign(cache.JointCount, std::array<int32_t, 3>{ -1, -1, -1 });

    const auto& tracks = clip.GetTracks();
    for (size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex)
    {
        const AnimationTrack& track = tracks[trackIndex];

        if (track.Times.empty() || track.Values.empty())
        {
            ++cache.IgnoredTrackCount;
            continue;
        }

        if (track.Joint >= cache.JointCount)
        {
            ++cache.IgnoredTrackCount;
            continue;
        }

        const size_t pathIndex = static_cast<size_t>(track.Path);
        if (pathIndex >= 3)
        {
            ++cache.IgnoredTrackCount;
            continue;
        }

        cache.TracksByJoint[track.Joint][pathIndex] = static_cast<int32_t>(trackIndex);
    }

    return cache;
}

void ResizePose(const Skeleton& skeleton, SkeletonPose& pose)
{
    const size_t jointCount = skeleton.GetJoints().size();

    if (pose.Local.size() != jointCount)
        pose.Local.assign(jointCount, JointPose{});
    if (pose.Global.size() != jointCount)
        pose.Global.assign(jointCount, glm::mat4(1.0f));
    if (pose.SkinMatrices.size() != jointCount)
        pose.SkinMatrices.assign(jointCount, glm::mat4(1.0f));
}

glm::vec4 SampleTrack(const AnimationTrack& track, float time)
{
    KBR_TRACY_FUNCTION();

    const auto& times = track.Times;
    const auto& values = track.Values;

    if (times.empty() || values.empty())
        return { 0.0f, 0.0f, 0.0f, 1.0f };

    const size_t keyCount = std::min(times.size(), values.size());
    if (keyCount == 1)
        return values[0];

    if (time <= times.front())
        return values.front();
    if (time >= times[keyCount - 1])
        return values[keyCount - 1];

    const auto upper = std::upper_bound(times.begin(), times.begin() + static_cast<ptrdiff_t>(keyCount), time);
    const size_t firstKey = static_cast<size_t>(upper - times.begin());
    const size_t previousKey = firstKey - 1;

    const float t0 = times[previousKey];
    const float t1 = times[firstKey];
    if (t1 <= t0)
        return values[previousKey];

    const float t = std::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f);

    switch (track.Interpolation)
    {
    case AnimationInterpolation::Step:
        return values[previousKey];

    case AnimationInterpolation::Linear:
    {
        if (track.Path == AnimationPath::Rotation)
        {
            const glm::quat start = ToQuat(values[previousKey]);
            const glm::quat end = ToQuat(values[firstKey]);
            return ToVec4(NormalizeOrIdentity(glm::slerp(start, end, t)));
        }

        return glm::mix(values[previousKey], values[firstKey], t);
    }

    case AnimationInterpolation::CubicSpline:
    {
        glm::vec4 p0 = values[previousKey];
        glm::vec4 p1 = values[firstKey];

        const glm::vec4 outTangent = previousKey < track.OutTangents.size()
            ? track.OutTangents[previousKey]
            : glm::vec4(0.0f);
        glm::vec4 inTangent = firstKey < track.InTangents.size()
            ? track.InTangents[firstKey]
            : glm::vec4(0.0f);

        if (track.Path == AnimationPath::Rotation && glm::dot(p0, p1) < 0.0f)
        {
            // Keep rotations on the same hemisphere so component-wise interpolation
            // follows the shortest path.
            p1 = -p1;
            inTangent = -inTangent;
        }

        const float interval = t1 - t0;
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 = t3 - 2.0f * t2 + t;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 = t3 - t2;

        const glm::vec4 value = h00 * p0 + h10 * interval * outTangent + h01 * p1 + h11 * interval * inTangent;

        if (track.Path == AnimationPath::Rotation)
            return ToVec4(NormalizeOrIdentity(glm::quat(value.w, value.x, value.y, value.z)));

        return { value.x, value.y, value.z, value.w };
    }

    }

    return values[previousKey];
}

void EvaluatePose(const Skeleton& skeleton,
                  const AnimationClip& clip,
                  const ClipTrackCache& cache,
                  const float time,
                  SkeletonPose& pose)
{
    KBR_TRACY_FUNCTION();

    const auto& joints = skeleton.GetJoints();
    ResizePose(skeleton, pose);

    for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
    {
        pose.Local[jointIndex] = JointPose{
            .Translation = joints[jointIndex].RestTranslation,
            .Rotation = joints[jointIndex].RestRotation,
            .Scale = joints[jointIndex].RestScale,
        };
    }

    const auto& tracks = clip.GetTracks();
    if (cache.TracksByJoint.size() == joints.size())
    {
        for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
        {
            const std::array<int32_t, 3>& slots = cache.TracksByJoint[jointIndex];

            const int32_t translationTrack = slots[static_cast<size_t>(AnimationPath::Translation)];
            if (translationTrack >= 0 && static_cast<size_t>(translationTrack) < tracks.size())
                pose.Local[jointIndex].Translation = glm::vec3(SampleTrack(tracks[translationTrack], time));

            const int32_t rotationTrack = slots[static_cast<size_t>(AnimationPath::Rotation)];
            if (rotationTrack >= 0 && static_cast<size_t>(rotationTrack) < tracks.size())
                pose.Local[jointIndex].Rotation = NormalizeOrIdentity(ToQuat(SampleTrack(tracks[rotationTrack], time)));

            const int32_t scaleTrack = slots[static_cast<size_t>(AnimationPath::Scale)];
            if (scaleTrack >= 0 && static_cast<size_t>(scaleTrack) < tracks.size())
                pose.Local[jointIndex].Scale = glm::vec3(SampleTrack(tracks[scaleTrack], time));
        }
    }

    ComputeGlobalAndSkinMatrices(skeleton, pose);
}

void EvaluateRestPose(const Skeleton& skeleton, SkeletonPose& pose)
{
    const auto& joints = skeleton.GetJoints();
    ResizePose(skeleton, pose);

    for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
    {
        pose.Local[jointIndex] = JointPose{
            .Translation = joints[jointIndex].RestTranslation,
            .Rotation = joints[jointIndex].RestRotation,
            .Scale = joints[jointIndex].RestScale,
        };
    }

    ComputeGlobalAndSkinMatrices(skeleton, pose);
}

void AdvancePlaybackTime(PlaybackTime& playback,
                         const float duration,
                         const bool looping,
                         const float speed,
                         const float dt,
                         const bool canAdvance)
{
    if (!std::isfinite(playback.Time))
        playback.Time = 0.0f;

    const bool validDuration = std::isfinite(duration) && duration > 0.0f;
    const bool shouldAdvance = canAdvance && validDuration &&
        std::isfinite(dt) && dt != 0.0f && std::isfinite(speed) && speed != 0.0f;

    if (shouldAdvance)
        playback.Time += dt * speed;

    if (!validDuration)
    {
        playback.Time = 0.0f;
        return;
    }

    if (looping)
    {
        playback.Time = std::fmod(playback.Time, duration);
        if (playback.Time < 0.0f)
            playback.Time += duration;
        return;
    }

    if (playback.Time <= 0.0f)
    {
        playback.Time = 0.0f;
        if (shouldAdvance && speed < 0.0f)
            playback.IsPlaying = false;
    }
    else if (playback.Time >= duration)
    {
        playback.Time = duration;
        if (shouldAdvance && speed > 0.0f)
            playback.IsPlaying = false;
    }
}

void ComputeGlobalAndSkinMatrices(const Skeleton& skeleton, SkeletonPose& pose)
{
    KBR_TRACY_FUNCTION();

    const auto& joints = skeleton.GetJoints();

    if (pose.Local.size() != joints.size() || pose.Global.size() != joints.size() ||
        pose.SkinMatrices.size() != joints.size())
    {
        ResizePose(skeleton, pose);
    }

    for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
    {
        const SkeletonJoint& joint = joints[jointIndex];
        const JointPose& localPose = pose.Local[jointIndex];

        const glm::mat4 local =
            glm::translate(glm::mat4(1.0f), localPose.Translation) *
            glm::toMat4(NormalizeOrIdentity(localPose.Rotation)) *
            glm::scale(glm::mat4(1.0f), localPose.Scale);

        const bool hasValidParent =
            joint.Parent >= 0 && static_cast<size_t>(joint.Parent) < jointIndex;

        pose.Global[jointIndex] = hasValidParent
            ? pose.Global[static_cast<size_t>(joint.Parent)] * local
            : local;

        pose.SkinMatrices[jointIndex] = pose.Global[jointIndex] * joint.InverseBind;
    }
}

}
