# AI context

Applies to `KerberosEngine/Source/AI/**`,
`KerberosEngine/Source/Scene/Components/AIComponents.hpp`,
`KerberosEngine/Source/Scene/Scene.*`,
`KerberosEngine/Source/Serialization/SceneSerializer.cpp`,
`KerberosEditor/Source/Windows/HierarchyPanel.cpp`, and the AI C# files under
`KerberosScriptCoreLib`.

## Architecture

The AI layer is a flat, data-driven state machine with a shared, per-entity
blackboard:

- `AIIdentifier.hpp` defines 64-bit FNV-1a IDs. `_bb` creates a
  `BlackboardKey`, `_id` creates a `StateID`, and `InvalidState` is 0.
- `AIStateTree` owns state definitions (ID, name, task prototypes, ordered
  transitions). Trees are immutable while entities are running.
- `AIStateTreeRegistry` maps names to `Ref<AIStateTree>` with an origin
  (`BuiltIn`, `Script`, `Asset`). Registering an existing name replaces it.
  Script-owned trees are cleared on project open and script-assembly reload;
  asset-owned trees have their own `ClearAssetTrees` path. Entries registered
  from a document also retain the authored `AIStateTreeDocument`, exposed via
  `GetDocument`; `GetOrigin` reports the registration origin.
- `AIStateTreeDocument` (`AIStateTreeDocument.hpp`) is the editable source
  representation: named states with task descriptors, ordered transitions with
  condition descriptors, entry state, and optional graph `EditorPosition`.
  `AIStateTreeSerializer` parses/serializes it as YAML or JSON and preserves
  parameter types against the registered node schema (for example an emitted
  `12` is restored as a float for a `Float` parameter). Unknown keys are
  ignored so older and C#-authored documents stay loadable.
- `AIStateTreeRegistry::CompileDocument` turns a document into an `AIStateTree`;
  `RegisterFromDocument` is the parse-compile-register path retained for C++/C#
  callers. The registry key is authoritative and is written back into the
  document name on registration.
- `AINodeFactory` maps string type names to task/condition factories. This is
  the extension point for code-registered nodes. Standard types:
  `ChaseTarget`, `FaceTarget`, `Stop`, `Wait`, `SetBlackboardBool`,
  `AttackTarget`, `DistanceToTarget`, `BlackboardBool`, `TimeInState`,
  `TargetVisible`, and `TargetExists`. Each registration also carries optional
  authoring metadata (`AINodeTypeInfo` + `AINodeParamDef`) that tools query with
  `GetAINodeTypes`/`FindAINodeType`. `CreateTask`/`CreateCondition` stamp the
  registered type name onto `IAITask::GetDebugType`/`IAICondition::GetDebugType`
  for diagnostics.
- `AIStateTreeRegistry::RegisterFromDocument` rejects duplicate states, unknown
  entry states, dangling transitions, unknown node types, and invalid
  parameters.

`AISystem` is owned by `Scene`, is a `friend` of `Scene`, and runs

1. perception (when `AIPerceptionComponent` is present),
2. state machine transitions and task ticking,
3. movement for `AIControllerComponent`.

The runtime order in Play/Simulate is transforms -> animation -> events ->
scripts (Play only) -> `AISystem::Update` -> ragdoll prepare -> physics ->
ragdoll readback -> finalize. AI must run before physics so kinematic bodies
receive the updated `TransformComponent` through
`JoltPhysicsSystem::SyncKinematicBodies`. Edit mode never ticks AI.

## Components

- `AIBlackboardComponent`: runtime `Values` keyed by `BlackboardKey`, plus
  authored `AuthoredValues` (name + `variant<bool, int64_t, float, vec3, UUID,
  string>`). Authored values are seeded with `try_emplace`, so script/runtime
  values win.
- `AIStateMachineComponent`: authored `TreeName` plus transient `Tree`,
  `CurrentState`, `PreviousState`, `PendingState`, `TimeInState`,
  `IsInitialized`, `LastTaskStatus`, and per-entity `TaskInstances`. Copying or
  duplicating an entity preserves `TreeName` but resets runtime state.
- `AIControllerComponent`: `Enabled`, `DesiredVelocity`, `LookAtTarget`,
  `HasLookAt`, `WantsToAttack`, and the transient `WarnedDynamicBody` flag.
  Tasks write intent; `AISystem::UpdateMovement` consumes and clears velocity.
- `AIPerceptionComponent`: target key, sight/lose radii, FOV, eye height, LOS
  requirement, perception interval, and faction.
- `AITargetableComponent`: faction marker for perceivable entities. Perception
  ignores same-faction targets.

Entity references are `UUID`s, never `entt::entity` handles. Always resolve with
`Scene::TryGetEntityByUUID`; `GetEntityByUUID` throws for stale IDs.

## State machine and tasks

- State IDs are hashes of state names; transitions are evaluated in vector
  order and the first all-true transition wins.
- `TimeInState` is advanced before transitions and is mirrored to the
  `StateTime` blackboard key.
- Tasks are prototypes on the tree and are cloned per entity at initialization.
  Never tick the prototypes directly or two enemies share task state.
- `PendingState` lets scripts force a state; it is consumed at the start of the
  next AI state-machine update.
- Task failure stops the remaining tasks for that tick and is recorded in
  `LastTaskStatus`; transitions remain condition-only.

## Perception and movement

Perception is throttled by `PerceptionInterval`, uses world positions from
`TransformComponent.WorldTransform[3]`, applies `LoseRadius` to the current
target and `SightRadius` to new targets, optionally checks FOV against the
entity's +Z forward axis, and optionally raycasts from the eye to the target.
No hit means visible; hits on self are ignored. Writes `TargetEntity` (`UUID`),
`HasLineOfSight` (`bool`), and `LastKnownPosition` (`vec3`).

Movement integrates `DesiredVelocity * dt` into the local `Translation` and
calls `Scene::CalculateEntityTransform` so the world transform is current
before Jolt. Facing writes `Rotation.y = atan2(dir.x, dir.z)` in radians
(+Z forward). AI agents must be Kinematic (or have no rigid body); dynamic
bodies are warned about because Jolt overwrites their transform. Parented
agents currently assume root-level local movement.

## Serialization

`SceneSerializer` persists only authored data:

- `AIBlackboardComponent`: `Values` list with `Key`, `Type`, `Value`.
- `AIStateMachineComponent`: `TreeName` only.
- `AIPerceptionComponent`: authored perception fields.
- `AIControllerComponent`: `Enabled` only.
- `AITargetableComponent`: `FactionId`.

Runtime state (`Values`, `CurrentState`, task instances, intent, perception
accumulator) is transient and rebuilt on the next start. The state machine
resolves `TreeName` through `AIStateTreeRegistry` lazily, so an unresolved tree
is a logged error and the agent is skipped rather than crashing.

## Editor and C#

`HierarchyPanel` adds an AI Agent compound entry (blackboard + state machine +
controller + perception), individual AI components, and an AI Targetable entry.
It draws tree selection, live state/time, perception/controller fields, and the
authored/runtime blackboard. Remove-component popups follow the other component
patterns.

C# uses `Kerberos.AI.AI.RegisterStateTree(name, docOrJson)` and the
`AIBlackboardComponent`, `AIStateMachineComponent`, `AIControllerComponent`,
`AIPerceptionComponent`, and `AITargetableComponent` proxies. Register script
trees in `OnCreate`; scripts are not instantiated in Simulate, so script-owned
trees are available in Play only. Native `BuiltIn` trees work in both.

The managed callback table in `ScriptInterface.cpp` is positional. Append new
callbacks at the end of both `NativeCallbackTable` and the C# `SetNativeCallbacks`
read sequence; never insert in the middle.

## Runtime debug capture

`AIDebug.hpp` defines the observation-only runtime data the AI Editor uses.
Capture is opt-in through `AISystem::SetDebugCaptureEnabled` and must stay
disabled outside the editor window. When disabled, `AISystem` adds no debug
allocations and does not change transition or task evaluation.

Each update while enabled produces one `AIAgentDebugSnapshot` per agent, keyed
by entity UUID, containing:

- the tree name and current/previous/pending state
- `TimeInState` and `LastTaskStatus`
- every task that ticked, its registered debug type, and result
- every transition candidate evaluated this tick, with per-condition results

`EvaluatedState` is the state whose transitions were evaluated this tick;
`CurrentState` is the state after any transition and is the state whose tasks
ticked. They differ when a transition happened in the same tick. Candidates
describe `EvaluatedState`, tasks describe `CurrentState`.

Transitions are recorded in a bounded 256-entry ring buffer with capture-relative
timestamps and a reason: `Initialize`, `Condition`, or `ScriptRequest`.
Initialization records `InvalidState -> Entry`; script requests record
`PendingState`; condition transitions use the transition's `DebugName`.
Only conditions and tasks that were actually evaluated are traced, because
evaluation stops on the first failing condition/task.

Enable/disable and `OnStart`/`OnStop` clear events, snapshots, and the capture
clock. Snapshots are cleared at the start of each captured update, so a paused
scene keeps its last snapshot. Snapshot pointers and event references are valid
until the next `AISystem::Update`; the panel must consume them in the same frame.
Blackboard values are not copied into snapshots; the panel reads
`AIBlackboardComponent` directly.

## Validation

Build and run the AI tests:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target KerberosEngineTests
.\out\build\vs2026\Tests\Debug\KerberosEngineTests.exe --gtest_filter=AI*
```

- Reconfigure CMake after adding new `.cpp` files; engine sources are globbed,
  but new test files must also be added to `Tests/CMakeLists.txt`.
- Tests cover hashing, typed blackboards, document parse/serialize round-trips
  (including typed parameters and editor positions), registry document/origin
  retention, compile rejection, node schema metadata, task/condition debug type
  stamps, entry and transitions, per-entity task instances, movement/facing,
  perception acquire/lose, subtree serialization, and debug capture
  (initialization, condition/script transitions, task failure, no-transition
  candidates, ring-buffer bounds, and capture clearing).
- Manual Play validation: add an AI Agent + Targetable target, assign a tree,
  verify transitions, movement, target loss, pause, save/load, duplicate, and a
  C#-registered tree after hot reload.
- LOS requires physics colliders; static meshes without a rigid body do not
  block sight.
- Keep this file synchronized with AI ownership, update order, serialization,
  node types, editor controls, or C# surface changes.
