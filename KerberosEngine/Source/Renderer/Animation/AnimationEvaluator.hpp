#pragma once

#include "AnimationClip.hpp"
#include "Skeleton.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Kerberos {

/// Local joint transform produced by animation pose evaluation.
struct JointPose
{
    glm::vec3 Translation{ 0.0f };
    glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 Scale{ 1.0f };
};

/// CPU side pose for one skeleton instance. Local is the canonical animated pose.
/// Global and SkinMatrices are derived from it.
struct SkeletonPose
{
    std::vector<JointPose> Local;
    std::vector<glm::mat4> Global;
    std::vector<glm::mat4> SkinMatrices;
};

/// Per-clip lookup from joint index and animation path to a track index in the clip.
/// Tracks that cannot be applied to the skeleton are reported through IgnoredTrackCount.
struct ClipTrackCache
{
    std::vector<std::array<int32_t, 3>> TracksByJoint;
    uint32_t JointCount = 0;
    uint32_t IgnoredTrackCount = 0;
};

/// Builds a clip lookup for the given skeleton. Tracks targeting joints outside the
/// skeleton, empty tracks, and duplicate path entries are skipped.
ClipTrackCache BuildClipTrackCache(const Skeleton& skeleton, const AnimationClip& clip);

/// Resizes the pose storage to the skeleton joint count. Existing values are kept when
/// the size already matches.
void ResizePose(const Skeleton& skeleton, SkeletonPose& pose);

/// Samples a single track at the given time in seconds. Returns the value as it is stored
/// by the importer (quaternion rotation are packed as xyz/w in a vec4).
glm::vec4 SampleTrack(const AnimationTrack& track, float time);

/// Fills the local pose from the skeleton rest pose and applies all compatible clip tracks.
void EvaluatePose(const Skeleton& skeleton,
                   const AnimationClip& clip,
                   const ClipTrackCache& cache,
                   float time,
                   SkeletonPose& pose);

/// Fills the local pose from the skeleton rest pose only.
void EvaluateRestPose(const Skeleton& skeleton, SkeletonPose& pose);

/// Computes Global and SkinMatrices from Local. 
/// Joints must be topologically sorted, which Skeleton enforces.
/// Malformed parents are treated as roots instead of indexing out of bounds.
void ComputeGlobalAndSkinMatrices(const Skeleton& skeleton, SkeletonPose& pose);

/// Runtime playback clock for one animator.
struct PlaybackTime
{
    float Time = 0.0f;
    bool IsPlaying = false;
};

/// Advances and wraps playback time for the current frame. A non-positive duration keeps the
/// clock at zero (looping with a zero duration must never divide by zero). Looping wraps into
/// [0, duration); otherwise the clock clamps and stops at the end matching the sign of speed.
void AdvancePlaybackTime(PlaybackTime& playback,
                         float duration,
                         bool looping,
                         float speed,
                         float dt,
                         bool canAdvance);

} // namespace Kerberos
