#pragma once

#include "AnimationClip.hpp"
#include "AnimationEventTypes.hpp"
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

/// Fills only the local pose from the skeleton rest pose and compatible clip tracks.
/// Use this when several clips are composed (cross-fades, ragdoll blending) and the
/// Global/SkinMatrices can be derived once from the composed result.
void EvaluatePoseLocal(const Skeleton& skeleton,
                       const AnimationClip& clip,
                       const ClipTrackCache& cache,
                       float time,
                       SkeletonPose& pose);

/// Fills the local pose from the skeleton rest pose only.
void EvaluateRestPose(const Skeleton& skeleton, SkeletonPose& pose);

/// Fills only the local pose from the skeleton rest pose.
void EvaluateRestPoseLocal(const Skeleton& skeleton, SkeletonPose& pose);

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

/// A portion of the clip timeline crossed by one frame. 
/// Wrapped spans are produced when playback loops. 
/// IncludeStart is true for the post-wrap segment so 
/// an event at time 0 can fire there.
struct PlaybackSpan
{
    float Start = 0.0f;
    float End = 0.0f;
    bool IncludeStart = false;
};

/// One event produced by crossing evaluation, before it is attached to an entity.
struct AnimationEventHit
{
    const AnimationEvent* Event = nullptr;
    int32_t EventIndex = -1;
    AnimationEventPhase Phase = AnimationEventPhase::Point;
    float Time = 0.0f;
};

/// State for a duration event that is currently active.
struct ActiveAnimationEvent
{
    int32_t EventIndex = -1;
    float Elapsed = 0.0f;
    bool HasBegun = false;
};

std::vector<PlaybackSpan> BuildPlaybackSpans(float fromTime,
                                             float toTime,
                                             float duration,
                                             bool looping);

bool IsAnimationWindowActive(const AnimationEvent& event,
                             float time,
                             float clipDuration,
                             bool looping);

void CollectPointEvents(const AnimationClip& clip,
                        float fromTime,
                        float toTime,
                        float duration,
                        bool looping,
                        float weight,
                        std::vector<AnimationEventHit>& out);

void UpdateDurationEvents(const AnimationClip& clip,
                          float fromTime,
                          float toTime,
                          float duration,
                          bool looping,
                          float weight,
                          std::vector<ActiveAnimationEvent>& active,
                          std::vector<AnimationEventHit>& out);

/// Blends two local poses. 
/// weight is the weight of b (0 = a, 1 = b). 
/// Rotations use shortest-path slerp.
/// Global/SkinMatrices are not recomputed. 
/// Poses with different joint counts fall back to the dominant pose instead of producing an invalid result.
void BlendPose(const SkeletonPose& a, const SkeletonPose& b, float weight, SkeletonPose& out);

/// Normalized weight of the incoming pose for a cross-fade. 
/// A non-positive or non-finite duration snaps to 1 so a 
/// zero-length fade always completes immediately.
float ComputeCrossFadeWeight(float elapsed, float duration);

/// Incrementally accumulates source into a weighted average of local poses. 
/// accumulatedWeight must start at 0 and is updated by this call. 
/// This is the shared composition primitive for layered poses. 
/// Two-pose cross-fades and ragdoll blending should use BlendPose.
void AccumulatePose(const SkeletonPose& source,
                    float weight,
                    SkeletonPose& target,
                    float& accumulatedWeight);

}
