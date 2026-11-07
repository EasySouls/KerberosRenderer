# Animation System Implementation Guide

## Recommended direction

Your engine already has the two hardest GPU pieces from that tutorial series:

- `SkinningSystem` already writes skinned vertices into a shared vertex buffer (SVB).
- `skinning.slang` already does 4-influence linear blend skinning with a compute dispatch and a post-dispatch barrier.
- `AnimationClip` already stores times, values, tangents, and interpolation mode.
- `Skeleton` already stores joint parents, rest TRS, and inverse-bind matrices.
- `AnimatorComponent` already references the skeleton entity and a clip list.

What is missing is the **pose evaluation layer** that turns `AnimatorComponent` + `AnimationClip` into per-joint local transforms, then into global and skin matrices.

My recommendation is:

1. **Make CPU pose evaluation authoritative.**
2. Keep GPU skinning as it is.
3. Treat GPU pose evaluation as a later optimization, not the first animation milestone.
4. Build the ragdoll handoff on the CPU joint pose, because Jolt is CPU-side.

This is also what the tutorial’s conclusion recommends: use bone-proxy colliders driven from CPU joint transforms, not GPU readback.

---

## 1. How the current system is wired

Current frame flow, roughly:

```text
EditorLayer::OnUpdate
  Scene::CalculateEntityTransforms
  Scene::OnUpdateRuntime/Simulation
    scripts
    PhysicsSystem::Update
Renderer::BeginFrame
  AnimationSystem::BeginFrame
    always writes bind-pose skin matrices
  SkinningSystem::BeginFrame
  AnimationSystem::AddPasses     // empty
  SkinningSystem::AddPasses      // compute skinning
```

Key current limitations:

- `AnimationSystem::BeginFrame` ignores `dt` and `paused`.
- It never reads `AnimatorComponent`.
- It always calls `WriteBindPose`.
- `SkinMatrixVersion` is incremented only when the skeleton instance is created, so `SkinningSystem` only dispatches once.
- `AnimationSystem::AddPasses` is empty.
- The renderer does not own an animation evaluation pass yet.
- Physics runs **before** animation evaluation, which is the reverse of what a ragdoll handoff needs.
- `SkinningSystem` re-dispatches only when `SkeletonFrame::SkinMatrixVersion` changes.

---

## 2. Target architecture

Separate the system into four layers:

```text
AnimatorComponent / AnimationClip / Skeleton
                │
                ▼
CPU pose evaluation
  local joint TRS
  global joint matrices
  skin matrices = global * inverseBind
                │
                ├──────────────► GPU skin matrix buffer
                │                       │
                │                       ▼
                │               SkinningSystem compute dispatch
                │                       │
                │                       ▼
                │                 shared vertex buffer (SVB)
                │
                ├──────────────► physics bone proxies / ragdoll
                │
                └──────────────► attachments, gameplay, debug drawing
```

The important boundary is:

- **CPU** owns the canonical animated pose and joint world transforms.
- **GPU** owns evaluation of per-vertex skinning and the shared vertex buffer.

That boundary keeps Jolt, gameplay, sockets/attachments, and ragdoll handoff simple.

If you later want GPU pose evaluation, the CPU still needs the authoritative joint transforms for physics. Do not make GPU pose evaluation the source of truth for ragdoll physics unless you accept a GPU-to-CPU readback.

---

## 3. Milestone 1: CPU animation sampling

### 3.1 Runtime pose state

Add a runtime-only pose structure, either inside `AnimationSystem` or a new `AnimationRuntime` owned by the scene:

```cpp
struct JointPose
{
    glm::vec3 Translation;
    glm::quat Rotation;
    glm::vec3 Scale;
};

struct SkeletonPose
{
    std::vector<JointPose> Local;
    std::vector<glm::mat4> Global;
    std::vector<glm::mat4> SkinMatrices;
};

struct SkeletonInstanceState
{
    UUID Entity;
    AssetHandle SkeletonAsset;
    Ref<Skeleton> Skeleton;

    uint32_t FirstSkinMatrix;
    uint32_t JointCount;
    uint64_t SkinMatrixVersion;

    SkeletonPose Pose;
    std::vector<int32_t> TrackLookup; // optional per-clip cache
};
```

Keep `AnimatorComponent` as authored data:

- `AnimationClips`
- `ActiveClip`
- `CurrentTime`
- `PlaybackSpeed`
- `IsPlaying`
- `IsLooping`
- `SkeletonEntity`

Keep resolved `Ref<AnimationClip>` handles in the runtime cache, not in serialized component state.

### 3.2 Which animator drives which skeleton

`AnimatorComponent` may be on the skeleton entity, or it may reference another entity through `SkeletonEntity`.

For each skeleton entity:

1. Look for an `AnimatorComponent` on the same entity.
2. If `AnimatorComponent::SkeletonEntity` is valid and different, look there.
3. If no animator exists, use the rest pose.
4. If multiple animators target the same skeleton, log a warning and choose one.

### 3.3 Time update

Per skeleton instance, per frame:

```cpp
if (animator.IsPlaying && !paused)
    animator.CurrentTime += dt * animator.PlaybackSpeed;

const float duration = clip ? clip->GetDuration() : 0.0f;

if (duration > 0.0f)
{
    if (animator.IsLooping)
    {
        animator.CurrentTime = std::fmod(animator.CurrentTime, duration);
        if (animator.CurrentTime < 0.0f)
            animator.CurrentTime += duration;
    }
    else
    {
        animator.CurrentTime = glm::clamp(animator.CurrentTime, 0.0f, duration);
        if (animator.CurrentTime >= duration)
            animator.IsPlaying = false;
    }
}
```

Decide explicitly whether animation advances in:

- `Edit`: yes if `IsPlaying`, for editor preview.
- `Simulate`: yes unless the scene is paused.
- `Play`: yes unless the scene is paused.

The animator UI already exposes `Playing`, `Time`, and `Looping`, so this can work as an editor preview immediately.

### 3.4 Sampling a clip

Start every joint from the skeleton rest pose:

```cpp
for (size_t i = 0; i < joints.size(); ++i)
{
    pose.Local[i] = {
        joints[i].RestTranslation,
        joints[i].RestRotation,
        joints[i].RestScale
    };
}
```

Then apply tracks. `AnimationClip` tracks are already remapped to the skeleton’s joint order by the importer.

For performance, build a per-clip lookup once:

```cpp
// joint index -> [translation track, rotation track, scale track]
std::array<int32_t, 3>
```

Use binary search or a cached interval index to find the keyframe pair for `CurrentTime`.

For each track:

- `Step`: use the previous key value.
- `Linear`:
  - Translation/scale: `glm::mix`.
  - Rotation: `glm::slerp` with shortest-path handling.
- `CubicSpline`:
  - Use the Hermite basis with `dt = t1 - t0`.
  - Translation/scale use vec3.
  - Rotation uses vec4 blending followed by `normalize`, per glTF.
  - Use `track.InTangents` and `track.OutTangents`.

Pseudo-code for cubic:

```cpp
float t = (time - t0) / (t1 - t0);
float dt = t1 - t0;
float t2 = t * t;
float t3 = t2 * t;

float h00 =  2*t3 - 3*t2 + 1;
float h10 =       t3 - 2*t2 + t;
float h01 = -2*t3 + 3*t2;
float h11 =       t3 -   t2;

value = h00 * p0 + h10 * dt * outTangent0
      + h01 * p1 + h11 * dt * inTangent1;
```

For rotations, interpolate the four components and normalize the result.

### 3.5 Global and skin matrices

After all local tracks are applied:

```cpp
for (size_t i = 0; i < joints.size(); ++i)
{
    const glm::mat4 local =
        glm::translate(glm::mat4(1.0f), localPose[i].Translation) *
        glm::toMat4(localPose[i].Rotation) *
        glm::scale(glm::mat4(1.0f), localPose[i].Scale);

    pose.Global[i] = joints[i].Parent >= 0
        ? pose.Global[joints[i].Parent] * local
        : local;

    pose.SkinMatrices[i] = pose.Global[i] * joints[i].InverseBind;
}
```

This is the same formula already used by `WriteBindPose`, except the local transforms now come from the clip instead of the rest pose.

### 3.6 Upload and versioning

Write `pose.SkinMatrices` into the existing skin matrix buffer.

Very important: **bump `SkinMatrixVersion` whenever the pose changes**.

`SkinningSystem` only dispatches when:

```cpp
instance.LastSkinnedVersion != skeletonFrame->SkinMatrixVersion
```

So:

- If the animator is playing, bump every frame.
- If the animator is paused and time did not change, do not bump.
- If the clip changes, bump.
- If the skeleton asset changes, reset/recreate the instance and bump.
- If no animator exists, keep the bind pose and do not bump every frame.

That gives you correct compute skinning with minimal dispatches.

---

## 4. Per-frame buffer safety

This is a critical engine-specific issue.

`AnimationSystem` currently has one persistent CPU-visible skin matrix buffer. Today it is only written at skeleton creation, so there is no race. Once you write it every frame, you can overwrite data still being read by an in-flight frame.

`VulkanContext` waits for the fence of the **current frame slot**, not for all frames in flight. A single shared skin matrix buffer is therefore unsafe.

Do one of these:

1. **Preferred:** use one skin matrix buffer per `MaxFramesInFlight`, selected by `frameIndex`.
2. Or wait for the device before every animation upload, which is expensive.

This applies to any future GPU animation pose/clip buffers as well.

If you keep the current single buffer temporarily, validate with synchronization validation and a two-frame-in-flight stress scene.

---

## 5. Milestone 2: cross-fade and ragdoll-aware blending

Implement pose blending as a reusable function:

```cpp
void BlendPose(const SkeletonPose& a, const SkeletonPose& b, float weight,
               SkeletonPose& out)
{
    for each joint:
        out.Local[i].Translation = glm::mix(a.Local[i].Translation, b.Local[i].Translation, weight);
        out.Local[i].Rotation    = glm::slerp(a.Local[i].Rotation, b.Local[i].Rotation, weight);
        out.Local[i].Scale       = glm::mix(a.Local[i].Scale, b.Local[i].Scale, weight);
}
```

Then compute globals/skin matrices from the blended local pose.

For clip-to-clip cross-fades, add runtime-only fields to the animator or to the animation runtime:

```cpp
int32_t PreviousClip;
float BlendElapsed;
float BlendDuration;
```

Do not serialize these unless you want authored transition state.

For ragdoll handoff, the same blending function blends:

- animation-driven local pose
- physics-driven local pose

using `RagdollWeight`.

The tutorial’s cross-fade section maps directly to this.

---

## 6. Milestone 3: optional GPU pose evaluation

Your `SkinningTypes.hpp` already sketches this design:

- `GPUSkeletonInfo`
- `GPUJointRest`
- `GPUAnimState`
- `GPUClipInfo`
- `GPUJointTrackMap`
- `GPUTrack`
- `EvalPush`
- `PrecomputePush`

If you choose to move clip evaluation to the GPU later, the sequence is:

```text
AnimationEval compute pass
  reads:  skeleton parents/rest, clip tracks, anim state
  writes: local pose T/R/S

SkinPrecompute compute pass
  reads:  local pose, parents, inverse-bind matrices
  writes: global pose and skin matrices

Skinning compute pass
  reads:  skin matrices, rest vertices, joint indices/weights
  writes: shared vertex buffer
```

Required barriers:

```text
EvalPose write -> Precompute read
Precompute write -> Skinning read
Skinning write -> vertex input read (already present)
```

You will need to add to `GPUTrack` either:

- separate key offsets for values, in-tangents, and out-tangents, or
- a key stride that lets the shader index all three arrays.

The current `GPUTrack` only has `FirstKey`/`KeyCount`, so it is not yet sufficient for cubic splines.

Also upload clip data once per clip, not per instance. Multiple characters using the same clip should share the track buffers.

Again: if you do this, Jolt still needs CPU joint transforms. Keep the CPU evaluator for physics or accept readback latency.

---

## 7. Milestone 4: physics and ragdolls

### 7.1 Use CPU joint transforms, not GPU readback

Jolt is CPU-side. Drive bone proxies from the `SkeletonPose::Global` matrices. The tutorial’s recommended approach is the right one:

- Visual mesh skinned on GPU.
- Physics uses capsule/sphere/box proxies attached to joints.
- No per-frame GPU vertex readback.

### 7.2 Bone proxy bodies

For each joint or bone segment:

- Create a kinematic Jolt body in `ANIMATED` state.
- Use a capsule aligned with the bone direction, or a sphere/box authored proxy.
- Place it from `pose.Global[joint]`.
- Store the body ID in a runtime ragdoll map, not in serialized `JointComponent` unless you want transient fields there.

You already have `RigidBody3DComponent` and capsule/box/sphere collider components. You can reuse them on joint entities, but the physics system currently recreates bodies when `IsDirty` is set, which would destroy constraints and velocities. For ragdolls you need explicit APIs:

- `SetMotionType(bodyId, Kinematic/Dynamic)`
- `MoveKinematic(bodyId, position, rotation, dt)`
- `SetLinearVelocity` / `SetAngularVelocity`
- `CreateConstraint(parentBody, childBody, settings)`
- `GetBodyTransform` / `GetBodyVelocity`

### 7.3 Constraints

For every parent-child joint pair, create a Jolt constraint:

- `SwingTwistConstraint` or `SixDOFConstraint` is a good general starting point.
- `HingeConstraint` for elbows/knees if you can author or infer the hinge axis.
- Store runtime constraint handles per joint.

Joint limits should come from:

- authored ragdoll data, or
- a first-pass heuristic from the rest pose and child direction.

Do not try to auto-generate perfect limits from glTF alone. It is better to start wide and let the user tune.

### 7.4 State machine

Recommended states per character/skeleton:

```text
ANIMATED
  animation drives joint pose
  all ragdoll bodies kinematic
  animation -> MoveKinematic before physics step

BLENDED
  bodies dynamic
  physics and animation poses both available
  ragdoll weight ramps 0 -> 1
  final pose = blend(animationPose, physicsPose, weight)

RAGDOLL
  physics drives joint pose
  animation may continue running for later recovery
  final pose = physics pose
```

At the handoff moment:

1. Snapshot the current animated joint velocities, not just positions.
2. Set body transforms to the current animated pose.
3. Set initial linear/angular velocities.
4. Switch bodies to dynamic.
5. Ramp the blend weight.

The tutorial warns about pose initialization, velocity initialization, and constraint resolution. Those are the three things that make the handoff look correct.

### 7.5 Frame order for ragdoll

Your current order is wrong for ragdoll:

```text
CalculateEntityTransforms
physics.Update
Renderer.BeginFrame
  animation evaluation
  skinning
```

That means physics sees the previous frame’s pose, and animation cannot feed the current physics result back into the same frame.

Change it to a pre/post-physics split:

```text
// Before physics
UpdateAnimationPose(dt)
  sample clips
  compute local/global joint poses
  move kinematic ragdoll bodies
  initialize velocities

CalculateEntityTransforms() // for non-physics-driven entities

// Physics
PhysicsSystem::Update(dt)

// After physics
ApplyAnimationPhysicsBlend()
  read ragdoll body transforms
  blend animation and physics poses
  recompute global and skin matrices
  optionally write joint entity WorldTransform for attachments/debug

// Renderer
upload skin matrices
dispatch skinning
```

For animation-only characters, the physics step is a no-op and the first pose pass is final.

For physics-driven joints, `CalculateEntityTransforms` must not overwrite your final joint world transforms. Keep the canonical pose in `AnimationSystem` and use it for skinning; only write `TransformComponent::WorldTransform` after the transform pass for attachments/debug. Do not write authored `Translation/Rotation/Scale`, or saving a scene during animation preview will bake the pose into the asset.

### 7.6 Self-collision filtering

Add ragdoll collision groups in Jolt:

- one group per ragdoll instance
- disable collisions between all bodies in the same ragdoll by default
- optionally allow hand/arm collisions later

Your current `Layers.hpp` has only `NON_MOVING` and `MOVING`. You will likely add a ragdoll object layer or use Jolt collision groups with a group filter table. Do not rely on the two-layer setup for character self-collision.

### 7.7 Ray tracing

Once physics/visual pose is stable, move the acceleration structure build after the skinning pass so BLAS refits use the animated SVB. Your current renderer builds acceleration structures before animation/skinning and has a TODO for this. The tutorial’s shared-vertex-buffer section maps directly to that.

---

## 8. Concrete integration checklist

Suggested order of work:

1. Add CPU pose state to `AnimationSystem` and sample `AnimationClip`.
2. Feed `AnimatorComponent` into `AnimationSystem::BeginFrame`; keep rest-pose fallback.
3. Bump `SkinMatrixVersion` when pose changes.
4. Verify CesiumMan animates with linear interpolation.
5. Add step and cubic-spline sampling; verify against glTF viewer.
6. Add per-frame skin matrix buffers or an explicit synchronization strategy.
7. Build clip cross-fades using the pose blend function.
8. Refactor update order so animation evaluates before scene transform/physics.
9. Add CPU pose exposure for gameplay/physics.
10. Add bone proxy bodies and constraints.
11. Implement `ANIMATED -> BLENDED -> RAGDOLL` state machine.
12. Add collision groups/self-collision filtering.
13. Move RT acceleration-structure work after skinning.
14. Add debug drawing: joint positions, bone axes, collider capsules, active clip/time.

---

## 9. Validation plan

- Compare bind pose against the glTF reference.
- Sample known keyframes at specific times and compare values.
- Test `STEP`, `LINEAR`, and `CUBICSPLINE`.
- Test looping with positive and negative playback speed.
- Test pause/resume and `Edit`/`Simulate`/`Play`.
- Test clip switching and cross-fades.
- Test multiple characters sharing one clip asset.
- Test scene switch and asset reload.
- Run Vulkan validation/synchronization validation with two frames in flight.
- For ragdoll: handoff at rest, mid-walk, and fast motion; verify no pose snap, no constraint explosion, and correct self-collision.
- Verify skinning and ray tracing use the same SVB.

---

## 10. Main pitfalls

- Do not keep `SkinMatrixVersion` static after the first update.
- Do not use one shared skin matrix buffer across frames in flight.
- Do not write animation output into authored joint `TransformComponent` translation/rotation/scale if you save scenes from Edit mode.
- Do not rely on `CalculateEntityTransforms` overwriting physics-driven joints.
- Do not use GPU readback for ragdoll physics.
- Do not assume a clip is compatible with any skeleton; validate joint counts/order.
- Do not forget quaternion shortest-path handling and normalization.
- Do not forget that cubic-spline tangents need interval `dt` scaling.
- Do not forget that `SkinningSystem` only re-dispatches on version changes.
- Do not forget that the current `SkinningSystem` mesh slices and output regions are never individually reclaimed; long-running prefab spawning will eventually need free lists.

The shortest path to a working animation milestone is: **CPU clip sampling → CPU global/skin matrices → existing GPU skinning → per-frame buffer safety**. Once that is stable, add blending, then ragdoll physics.
