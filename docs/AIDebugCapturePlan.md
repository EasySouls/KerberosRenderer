# AI System Debug Capture Plan

Phase 4 of the AI Editor plan: runtime reflection support for the AI state machine.

This phase is independent of the asset work (`.kbrai` assets and `TreeAsset` binding).
It only depends on the existing `AIStateTree`, `AIStateMachineComponent`, and
`AISystem` update loop. The AI Editor panel will later toggle capture and read
these APIs.

## Goal

Add opt-in debug capture to `AISystem` so the editor can show:

- The current, previous, pending, and evaluated state of each agent.
- Time in state and the last task status.
- Every task that ticked, its registered type, and its result.
- Every transition candidate evaluated this tick, including per-condition results.
- A bounded history of transitions with the reason they occurred.

Capture is disabled by default and is intended to be enabled only while the AI
Editor window is open. When capture is disabled, `AISystem` must not allocate
debug data or change its evaluation behavior.

## Scope

In scope:

- New debug data types for snapshots, task traces, condition traces, and transition events.
- Opt-in capture control in `AISystem`.
- Snapshot storage keyed by entity UUID.
- A bounded transition ring buffer.
- Capture points in initialization, pending-state requests, condition-driven transitions, and task ticking.
- Tests and subsystem documentation.

Out of scope:

- The AI Editor panel itself (phase 5).
- `.kbrai` assets, the asset pipeline, and `TreeAsset` binding (phases 2 and 3).
- Serializing debug data.
- Copying blackboard values into snapshots; the panel should read `AIBlackboardComponent` directly.

## Data Types

Create `KerberosEngine/Source/AI/AIDebug.hpp` so the editor panel and tests can
include the debug types without pulling in all of `AISystem`.

```cpp
enum class AITransitionReason : std::uint8_t
{
    Initialize,
    Condition,
    ScriptRequest
};

struct AITaskDebugTrace
{
    std::string Type;                 // IAITask::GetDebugType()
    TaskStatus Status = TaskStatus::Running;
};

struct AIConditionDebugTrace
{
    std::string Type;                 // IAICondition::GetDebugType()
    bool Result = false;
};

struct AITransitionDebugTrace
{
    std::string Name;                 // AITransition::DebugName
    StateID TargetState = InvalidState;
    std::string TargetStateName;
    bool Satisfied = false;
    std::vector<AIConditionDebugTrace> Conditions;
};

struct AIAgentDebugSnapshot
{
    bool Valid = false;
    UUID Entity = UUID::Invalid();
    std::string TreeName;

    StateID EvaluatedState = InvalidState;   // state whose transitions were evaluated
    StateID CurrentState = InvalidState;     // state after any transition this tick
    StateID PreviousState = InvalidState;
    StateID PendingState = InvalidState;

    float TimeInState = 0.0f;
    TaskStatus LastTaskStatus = TaskStatus::Running;

    std::vector<AITaskDebugTrace> Tasks;
    std::vector<AITransitionDebugTrace> Candidates;
};

struct AITransitionEvent
{
    double Time = 0.0;                       // capture-relative seconds
    UUID Entity = UUID::Invalid();
    StateID FromState = InvalidState;
    StateID ToState = InvalidState;
    std::string FromStateName;
    std::string ToStateName;
    std::string TransitionName;
    AITransitionReason Reason = AITransitionReason::Condition;
    float TimeInPreviousState = 0.0f;
};
```

Use an enum for `Reason` rather than free-form strings. The panel can map values
to labels such as `Init`, `Condition`, and `Script`.

## AISystem API and Storage

Add to `AISystem.hpp`:

```cpp
public:
    void SetDebugCaptureEnabled(bool enabled);
    [[nodiscard]] bool IsDebugCaptureEnabled() const;

    [[nodiscard]] const std::vector<AITransitionEvent>& GetTransitionEvents() const;
    [[nodiscard]] const AIAgentDebugSnapshot* GetDebugSnapshot(UUID entity) const;

private:
    void RecordTransition(const AIStateMachineComponent& stateMachine,
                          UUID entity,
                          StateID from,
                          StateID to,
                          std::string_view transitionName,
                          AITransitionReason reason,
                          float timeInPreviousState);

    void ClearDebugCapture();

    bool m_DebugCaptureEnabled = false;
    double m_DebugTime = 0.0;
    std::deque<AITransitionEvent> m_TransitionEvents;
    std::unordered_map<UUID, AIAgentDebugSnapshot> m_DebugSnapshots;
    static constexpr std::size_t MaxTransitionEvents = 256;
```

`SetDebugCaptureEnabled` should only clear data when the flag actually changes:

```cpp
void AISystem::SetDebugCaptureEnabled(const bool enabled)
{
    if (m_DebugCaptureEnabled == enabled)
        return;

    m_DebugCaptureEnabled = enabled;
    ClearDebugCapture();
}
```

`ClearDebugCapture` clears the event buffer, snapshots, and `m_DebugTime`.

## Capture Flow

### `AISystem::Update`

When capture is enabled:

```cpp
if (m_DebugCaptureEnabled)
{
    m_DebugTime += dt;
    m_DebugSnapshots.clear();   // snapshots describe the current frame only
}
```

Only clear and reallocate when capture is on. Disabled capture must add no
allocations and change no behavior.

### `AISystem::EnsureInitialized`

The entry state is set directly in `EnsureInitialized`, not through
`TransitionTo`. After setting the entry state and calling `OnEnter`, record an
initialization event:

```cpp
RecordTransition(stateMachine, entity.GetUUID(),
                 InvalidState, entryState,
                 "Entry", AITransitionReason::Initialize, 0.0f);
```

This gives the transition log an initial `→ Idle` entry.

### `AISystem::TransitionTo`

`TransitionTo` is currently `static`. Make it a non-static member so it can
write the ring buffer, and add parameters:

```cpp
void TransitionTo(Entity entity,
                  AIBlackboardComponent& blackboard,
                  AIStateMachineComponent& stateMachine,
                  StateID targetState,
                  AIContext& context,
                  std::string_view transitionName,
                  AITransitionReason reason);
```

Capture the previous time before resetting `TimeInState`:

```cpp
const float previousTime = stateMachine.TimeInState;

// existing OnExit / state change / OnEnter logic

RecordTransition(stateMachine, entity.GetUUID(),
                 stateMachine.PreviousState, targetState,
                 transitionName, reason, previousTime);
```

Call sites:

- Pending state: `AITransitionReason::ScriptRequest`, name `"PendingState"` or empty.
- Condition transition: `AITransitionReason::Condition`, name `transition.DebugName`.

`RecordTransition` resolves `FromStateName` and `ToStateName` through
`stateMachine.Tree->FindState(...)`, skips names for `InvalidState`, pushes the
event into the deque, and trims the front when the buffer exceeds
`MaxTransitionEvents`.

### `AISystem::UpdateStateMachine`

`UpdateStateMachine` is currently `const`; make it non-const. Build one local
`AIAgentDebugSnapshot` per agent when capture is enabled.

Order of capture:

1. After `TimeInState += dt`, fill `TimeInState`, `TreeName`, and the current,
   previous, and pending state fields.
2. Before evaluating transitions, set `EvaluatedState = CurrentState`.
3. During the existing transition loop, collect candidate traces without
   re-evaluating conditions:

```cpp
for (const AITransition& transition : currentDefinition->Transitions)
{
    AITransitionDebugTrace* trace = nullptr;
    if (m_DebugCaptureEnabled)
    {
        snapshot.Candidates.emplace_back();
        trace = &snapshot.Candidates.back();
        trace->Name = transition.DebugName;
        trace->TargetState = transition.TargetState;
        if (const AIStateDefinition* target = stateMachine.Tree->FindState(transition.TargetState))
            trace->TargetStateName = target->Name;
    }

    bool conditionsMet = true;
    for (const Ref<IAICondition>& condition : transition.Conditions)
    {
        const bool result = condition && condition->TestCondition(context);

        if (trace)
        {
            trace->Conditions.push_back({
                condition ? condition->GetDebugType() : std::string("<null>"),
                result
            });
        }

        if (!result)
        {
            conditionsMet = false;
            break;
        }
    }

    if (trace)
        trace->Satisfied = conditionsMet;

    if (conditionsMet)
    {
        nextState = transition.TargetState;
        winningTransition = &transition;
        break;
    }
}
```

The trace must reflect the real evaluation. Do not run conditions twice.
Conditions after the first failure are not evaluated and therefore not traced;
that is correct and should be documented.

4. After any transition and re-fetching `currentDefinition`, tick tasks. While
   doing so, record each task trace:

```cpp
const TaskStatus status = task->OnTick(context);
stateMachine.LastTaskStatus = status;

if (m_DebugCaptureEnabled)
    snapshot.Tasks.push_back({ task->GetDebugType(), status });

if (status == TaskStatus::Failed)
    break;
```

5. At the end, fill the post-tick fields and store the snapshot:

```cpp
if (m_DebugCaptureEnabled)
{
    snapshot.CurrentState = stateMachine.CurrentState;
    snapshot.PreviousState = stateMachine.PreviousState;
    snapshot.PendingState = stateMachine.PendingState;
    snapshot.LastTaskStatus = stateMachine.LastTaskStatus;
    snapshot.Valid = true;
    m_DebugSnapshots[entity.GetUUID()] = std::move(snapshot);
}
```

### `AISystem::OnStart` and `OnStop`

Clear debug events, snapshots, and time. This avoids stale data across play
sessions.

## Snapshot Semantics

A transition can happen in the same tick that transitions are evaluated. Keep
the distinction explicit:

- `EvaluatedState` — the state whose transitions were evaluated this tick.
- `CurrentState` — the state after any transition; its tasks were ticked.
- If no transition happened, they are equal.
- `Candidates` describes `EvaluatedState`.
- `Tasks` describes `CurrentState`.
- `PreviousState` remains the component's previous-state field.

The AI Editor should label the candidate section as
“Transitions from `<EvaluatedState>`” so the data is not confusing.

## Tests

Add `Tests/AIDebugTests.cpp` and list it in `Tests/CMakeLists.txt`, or extend
`Tests/AITests.cpp`. Cases:

1. **Capture disabled** — no events and no snapshot.
2. **Initialization** — enabling capture before `OnStart` produces an
   `Initialize` event from `InvalidState` to the entry state.
3. **Condition transition** — candidate traces include the target and condition
   results; the transition event has `Reason=Condition`, correct from/to names,
   and nonzero `TimeInPreviousState`.
4. **Script request** — setting `PendingState` produces a `ScriptRequest` event.
5. **Task failure** — a custom `FailingTask` followed by another task traces only
   the failing task and reports `LastTaskStatus=Failed`.
6. **No transition** — candidates exist, all `Satisfied=false`, and
   `EvaluatedState == CurrentState`.
7. **Ring buffer cap** — drive more than 256 transitions and assert the deque
   stays bounded and drops the oldest.
8. **Clearing** — `OnStop` and `SetDebugCaptureEnabled(false)` clear events and
   snapshots.

A small helper for failure tests:

```cpp
class FailingTask final : public TAITask<FailingTask>
{
public:
    TaskStatus OnTick(AIContext&) override { return TaskStatus::Failed; }
};
```

## Documentation

Update `KerberosEngine/Source/AI/AGENTS.md` with a runtime-debug section
covering:

- Opt-in capture, enabled only while the AI Editor is open.
- `AIAgentDebugSnapshot`, task/condition traces, and transition candidates.
- `EvaluatedState` vs `CurrentState` semantics.
- The bounded 256-entry transition ring buffer and its reasons
  (`Initialize`, `Condition`, `ScriptRequest`).
- Capture-relative timestamps.
- Only evaluated conditions and tasks are traced because evaluation stops on
  first failure.
- Data is cleared on enable/disable and on `OnStart`/`OnStop`.
- No blackboard copy; the panel reads `AIBlackboardComponent` directly.
- Test and validation commands.

## Files to Touch

| File | Change |
|---|---|
| `KerberosEngine/Source/AI/AIDebug.hpp` | New debug structs and enums (recommended). |
| `KerberosEngine/Source/AI/AISystem.hpp` | API, storage, non-const `UpdateStateMachine`, member `TransitionTo`, `RecordTransition`, `ClearDebugCapture`. |
| `KerberosEngine/Source/AI/AISystem.cpp` | Capture in `Update`, `EnsureInitialized`, `TransitionTo`, `UpdateStateMachine`, `OnStart`, `OnStop`. |
| `Tests/AIDebugTests.cpp` | New tests. |
| `Tests/CMakeLists.txt` | Add the new test file. |
| `KerberosEngine/Source/AI/AGENTS.md` | Document debug capture. |

## Pitfalls

- Do not evaluate conditions or tasks twice for tracing.
- Make `UpdateStateMachine` non-const and `TransitionTo` a member; otherwise the
  ring buffer cannot be written.
- Clear snapshots per update only when capture is enabled, so paused scenes keep
  their last snapshot.
- Do not serialize any of this data.
- Do not add mutexes; `AISystem` runs on the main thread.
- Returned snapshot pointers and event references are valid until the next
  `AISystem::Update`, so the panel must consume them in the same frame.
- Keep the existing transition and task evaluation order and early exits exactly
  as they are; capture must be observation-only.
