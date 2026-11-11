#include "AIEditorInspector.hpp"

#include "AIEditorConversions.hpp"
#include "AIEditorNodeSchema.hpp"

#include "AI/AIDebug.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Profiling/TracyInstrumentation.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace Kerberos {

namespace {

const char* StatusLabel(const AI::TaskStatus status)
{
    switch (status)
    {
    case AI::TaskStatus::Running:   return "Running";
    case AI::TaskStatus::Succeeded: return "Succeeded";
    case AI::TaskStatus::Failed:    return "Failed";
    }
    return "Unknown";
}

ImVec4 StatusColor(const AI::TaskStatus status)
{
    switch (status)
    {
    case AI::TaskStatus::Running:   return ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
    case AI::TaskStatus::Succeeded: return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
    case AI::TaskStatus::Failed:    return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    }
    return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
}

template <typename T>
T ParamOr(const AI::AINodeParam& param, const AI::AINodeParam& fallback)
{
    if (const T* value = std::get_if<T>(&param))
        return *value;
    return std::get<T>(fallback);
}

std::string StateNameOrNone(const AIEditorDocument& document, const uint64_t editorID)
{
    std::string name = ResolveStateName(document, editorID);
    return name.empty() ? std::string("<none>") : name;
}

} // namespace

void AIEditorInspector::Draw(AIEditorTab& tab, bool canEdit, const Ref<Scene>& scene,
                             const bool isRunning)
{
    KBR_TRACY_FUNCTION();
    
    AIEditorDocument& document = tab.Document;
    if (document.ViewOnly)
        canEdit = false;

    // A transition selection is more specific than the source state selection.
    if (AIEditorTransitionRef* transitionRef = document.FindTransition(tab.SelectedTransitionID))
    {
        DrawTransitionInspector(tab, document, *transitionRef, canEdit, scene, isRunning);
        return;
    }

    if (AIEditorStateRef* stateRef = document.FindState(tab.SelectedStateID))
    {
        DrawStateInspector(tab, document, *stateRef, canEdit, scene, isRunning);
        return;
    }

    ImGui::TextDisabled("Select a state or transition in the graph.");
}

void AIEditorInspector::DrawStateInspector(AIEditorTab& tab,
                                           AIEditorDocument& document,
                                           AIEditorStateRef& stateRef,
                                           const bool canEdit,
                                           const Ref<Scene>& activeScene,
                                           const bool isRunning)
{
    KBR_TRACY_FUNCTION();

    AI::AIDocumentState* state = document.ResolveState(stateRef);
    if (!state)
        return;

    ImGui::Text("State");
    ImGui::SameLine();
    ImGui::TextDisabled("#%llu", stateRef.EditorID);

    std::snprintf(m_StateNameBuffer, sizeof(m_StateNameBuffer), "%s", state->Name.c_str());

    ImGui::BeginDisabled(!canEdit);
    if (ImGui::InputText("Name", m_StateNameBuffer, sizeof(m_StateNameBuffer)))
    {
        // Live-write so the buffer and model stay in sync while typing.
        document.RenameState(stateRef.EditorID, m_StateNameBuffer);
    }
    TrackEdit(tab, document, "Rename State");
    ImGui::EndDisabled();

    const bool isEntry = document.Document.EntryState == state->Name;
    ImGui::BeginDisabled(!canEdit || isEntry);
    if (ImGui::Button("Set as Entry State"))
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        document.Document.EntryState = state->Name;
        tab.RecordDocumentEdit(before, "Set Entry State");
    }
    ImGui::EndDisabled();

    ImGui::TextDisabled("Position: %.0f, %.0f",
                        state->EditorPosition.x, state->EditorPosition.y);

    DrawNodeList(tab, document, state->Tasks, AI::AINodeKind::Task, "Tasks", canEdit);

    // Keep runtime read-only and draw it before structural actions below,
    // which may rebuild the session references.
    if (isRunning && activeScene)
        DrawStateRuntime(tab, document, stateRef, activeScene);

    ImGui::SeparatorText("Outgoing Transitions");
    ImGui::PushID("OutgoingTransitions");

    int removeIndex = -1;
    int moveFromIndex = -1;
    int moveToIndex = -1;

    for (const AIEditorTransitionRef& ref : document.Transitions)
    {
        if (ref.SourceStateIndex != stateRef.DocumentStateIndex)
            continue;

        const AI::AIDocumentTransition* transition = document.ResolveTransition(ref);
        if (!transition)
            continue;

        ImGui::PushID(static_cast<int>(ref.EditorID));

        const std::string label = std::format("{} -> {} (priority {})",
            transition->Name.empty() ? "<unnamed>" : transition->Name,
            transition->TargetState,
            ref.TransitionIndex + 1);

        if (ImGui::Selectable(label.c_str(), tab.SelectedTransitionID == ref.EditorID))
        {
            tab.SelectedTransitionID = ref.EditorID;
            tab.SelectedStateID = 0;
            tab.PendingLinkSelection = ref.EditorID;
            tab.PendingSelectionReveal = true;
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!canEdit || ref.TransitionIndex == 0);
        if (ImGui::SmallButton("Up"))
        {
            moveFromIndex = static_cast<int>(ref.TransitionIndex);
            moveToIndex = static_cast<int>(ref.TransitionIndex) - 1;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!canEdit || ref.TransitionIndex + 1 >= state->Transitions.size());
        if (ImGui::SmallButton("Down"))
        {
            moveFromIndex = static_cast<int>(ref.TransitionIndex);
            moveToIndex = static_cast<int>(ref.TransitionIndex) + 1;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!canEdit);
        if (ImGui::SmallButton("Remove"))
            removeIndex = static_cast<int>(ref.TransitionIndex);
        ImGui::EndDisabled();

        ImGui::PopID();
    }

    ImGui::BeginDisabled(!canEdit || document.Document.States.empty());
    if (ImGui::Button("Add Transition"))
    {
        std::string target = state->Name;
        for (const AI::AIDocumentState& candidate : document.Document.States)
        {
            if (candidate.Name != state->Name)
            {
                target = candidate.Name;
                break;
            }
        }

        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        const uint64_t id = document.AddTransition(
            stateRef.DocumentStateIndex, target,
            document.GenerateUniqueTransitionName(*state));

        tab.RecordDocumentEdit(before, "Add Transition");
        tab.SelectedStateID = 0;
        tab.SelectedTransitionID = id;
        tab.PendingLinkSelection = id;
        tab.PendingSelectionReveal = true;

        ImGui::PopID();
        ImGui::EndDisabled();
        return;
    }
    ImGui::EndDisabled();
    ImGui::PopID();

    if (removeIndex >= 0)
    {
        const std::pair indices{
            stateRef.DocumentStateIndex, static_cast<size_t>(removeIndex)
        };
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        document.RemoveTransitions(std::span<const std::pair<size_t, size_t>>(&indices, 1));
        tab.RecordDocumentEdit(before, "Delete Transition");
        tab.ResetGraphSession();
    }
    else if (moveFromIndex >= 0 && moveToIndex >= 0)
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        const uint64_t movedID = document.MoveTransitionByIndex(
            stateRef.DocumentStateIndex,
            static_cast<size_t>(moveFromIndex),
            static_cast<size_t>(moveToIndex));

        tab.RecordDocumentEdit(before, "Reorder Transition");
        tab.ResetGraphSession();
        tab.SelectedTransitionID = movedID;
        tab.PendingLinkSelection = movedID;
        tab.PendingSelectionReveal = true;
    }
}

void AIEditorInspector::DrawTransitionInspector(AIEditorTab& tab,
                                                AIEditorDocument& document,
                                                AIEditorTransitionRef& transitionRef,
                                                const bool canEdit,
                                                const Ref<Scene>& activeScene,
                                                const bool isRunning)
{
    KBR_TRACY_FUNCTION();
    
    AI::AIDocumentTransition* transition = document.ResolveTransition(transitionRef);
    if (!transition)
        return;

    ImGui::Text("Transition");
    ImGui::SameLine();
    ImGui::TextDisabled("#%llu", transitionRef.EditorID);

    std::snprintf(m_TransitionNameBuffer, sizeof(m_TransitionNameBuffer),
                  "%s", transition->Name.c_str());

    ImGui::BeginDisabled(!canEdit);
    if (ImGui::InputText("Name", m_TransitionNameBuffer, sizeof(m_TransitionNameBuffer)))
        transition->Name = m_TransitionNameBuffer;
    TrackEdit(tab, document, "Rename Transition");
    ImGui::EndDisabled();

    const std::string beforeTarget = transition->TargetState;
    ImGui::BeginDisabled(!canEdit);
    const bool targetComboOpen = ImGui::BeginCombo("Target State", transition->TargetState.c_str());
    const unsigned int targetComboID = ImGui::GetItemID();
    if (ImGui::IsItemActivated())
    {
        m_ComboPending = CaptureSnapshot(document);
        m_ComboPendingID = targetComboID;
    }

    if (targetComboOpen)
    {
        for (const AI::AIDocumentState& candidate : document.Document.States)
        {
            const bool selected = candidate.Name == transition->TargetState;
            if (ImGui::Selectable(candidate.Name.c_str(), selected))
                transition->TargetState = candidate.Name;
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (transition->TargetState != beforeTarget &&
        m_ComboPending && m_ComboPendingID == targetComboID)
    {
        tab.RecordDocumentEdit(std::move(*m_ComboPending), "Retarget Transition");
        m_ComboPending.reset();
        m_ComboPendingID = 0;
    }

    DrawNodeList(tab, document, transition->Conditions, AI::AINodeKind::Condition,
                 "Conditions", canEdit);

    if (isRunning && activeScene)
        DrawTransitionRuntime(tab, document, transitionRef, activeScene);

    ImGui::SeparatorText("Priority");
    ImGui::TextDisabled("%zu of %zu",
                        transitionRef.TransitionIndex + 1,
                        document.Document.States[transitionRef.SourceStateIndex].Transitions.size());

    ImGui::BeginDisabled(!canEdit || transitionRef.TransitionIndex == 0);
    if (ImGui::Button("Move Up"))
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        const uint64_t movedID = document.MoveTransitionByIndex(
            transitionRef.SourceStateIndex,
            transitionRef.TransitionIndex,
            transitionRef.TransitionIndex - 1);

        tab.RecordDocumentEdit(before, "Reorder Transition");
        tab.ResetGraphSession();
        tab.SelectedTransitionID = movedID;
        tab.PendingLinkSelection = movedID;
        tab.PendingSelectionReveal = true;
        ImGui::EndDisabled();
        return;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!canEdit ||
        transitionRef.TransitionIndex + 1 >=
            document.Document.States[transitionRef.SourceStateIndex].Transitions.size());
    if (ImGui::Button("Move Down"))
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        const uint64_t movedID = document.MoveTransitionByIndex(
            transitionRef.SourceStateIndex,
            transitionRef.TransitionIndex,
            transitionRef.TransitionIndex + 1);

        tab.RecordDocumentEdit(before, "Reorder Transition");
        tab.ResetGraphSession();
        tab.SelectedTransitionID = movedID;
        tab.PendingLinkSelection = movedID;
        tab.PendingSelectionReveal = true;
        ImGui::EndDisabled();
        return;
    }
    ImGui::EndDisabled();
}

void AIEditorInspector::DrawNodeList(AIEditorTab& tab,
                                     AIEditorDocument& document,
                                     std::vector<AI::AINodeDesc>& nodes,
                                     const AI::AINodeKind kind,
                                     const char* label,
                                     const bool canEdit)
{
    KBR_TRACY_FUNCTION();
    
    ImGui::SeparatorText(label);
    ImGui::PushID(label);

    int removeIndex = -1;
    int moveUpIndex = -1;
    int moveDownIndex = -1;
    const size_t count = nodes.size();

    for (size_t i = 0; i < count; ++i)
    {
        AI::AINodeDesc& node = nodes[i];
        ImGui::PushID(static_cast<int>(i));

        const bool open = ImGui::TreeNodeEx("##node",
            ImGuiTreeNodeFlags_DefaultOpen, "%s", node.Type.c_str());

        ImGui::SameLine();
        ImGui::BeginDisabled(!canEdit);
        if (ImGui::SmallButton("Up"))
            moveUpIndex = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("Down"))
            moveDownIndex = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            removeIndex = static_cast<int>(i);
        ImGui::EndDisabled();

        if (open)
        {
            ImGui::BeginDisabled(!canEdit);
            if (ImGui::BeginCombo("Type", node.Type.c_str()))
            {
                for (const AI::AINodeTypeInfo& candidate : AI::GetAINodeTypes(kind))
                {
                    if (ImGui::Selectable(candidate.Type.c_str(), candidate.Type == node.Type))
                    {
                        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
                        node = MakeDefaultNodeDesc(candidate);
                        tab.RecordDocumentEdit(before, "Change Node Type");
                        break;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();

            if (const std::optional<AI::AINodeTypeInfo> info =
                    AI::FindAINodeType(node.Type, kind))
            {
                DrawNodeParams(tab, document, node, *info, canEdit);
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                   "Unknown %s type: %s",
                                   kind == AI::AINodeKind::Task ? "task" : "condition",
                                   node.Type.c_str());
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        nodes.erase(nodes.begin() + removeIndex);
        tab.RecordDocumentEdit(before, "Remove Node");
    }
    else if (moveUpIndex > 0)
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        std::swap(nodes[static_cast<size_t>(moveUpIndex)],
                  nodes[static_cast<size_t>(moveUpIndex - 1)]);
        tab.RecordDocumentEdit(before, "Move Node Up");
    }
    else if (moveDownIndex >= 0 && static_cast<size_t>(moveDownIndex + 1) < nodes.size())
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
        std::swap(nodes[static_cast<size_t>(moveDownIndex)],
                  nodes[static_cast<size_t>(moveDownIndex + 1)]);
        tab.RecordDocumentEdit(before, "Move Node Down");
    }

    ImGui::BeginDisabled(!canEdit);
    if (ImGui::BeginCombo("Add##addNode", "Add..."))
    {
        for (const AI::AINodeTypeInfo& info : AI::GetAINodeTypes(kind))
        {
            if (ImGui::Selectable(info.Type.c_str()))
            {
                const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
                nodes.push_back(MakeDefaultNodeDesc(info));
                tab.RecordDocumentEdit(before, "Add Node");
                break;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    ImGui::PopID();
}

void AIEditorInspector::DrawNodeParams(AIEditorTab& tab,
                                       const AIEditorDocument& document,
                                       AI::AINodeDesc& node,
                                       const AI::AINodeTypeInfo& info,
                                       const bool canEdit)
{
    KBR_TRACY_FUNCTION();
    
    ImGui::BeginDisabled(!canEdit);

    for (const AI::AINodeParamDef& def : info.Params)
    {
        ImGui::PushID(def.Name.c_str());

        // Do not materialize schema defaults by merely inspecting a node. Only
        // insert a parameter when the user actually changes it.
        const auto existing = node.Params.find(def.Name);
        AI::AINodeParam value =
            existing != node.Params.end() ? existing->second : def.Default;
        bool changed = false;

        bool comboOpen = false;
        bool comboSelected = false;
        unsigned int comboID = 0;

        switch (def.Type)
        {
        case AI::AINodeParamType::Bool:
        {
            bool current = ParamOr<bool>(value, def.Default);
            if (ImGui::Checkbox(def.Name.c_str(), &current))
            {
                value = current;
                changed = true;
            }
            break;
        }
        case AI::AINodeParamType::Int:
        {
            std::int64_t current = ParamOr<std::int64_t>(value, def.Default);
            if (ImGui::DragScalar(def.Name.c_str(), ImGuiDataType_S64, &current, 1.0f))
            {
                value = current;
                changed = true;
            }
            break;
        }
        case AI::AINodeParamType::Float:
        {
            double current = ParamOr<double>(value, def.Default);
            if (ImGui::DragScalar(def.Name.c_str(), ImGuiDataType_Double, &current, 0.05f))
            {
                value = current;
                changed = true;
            }
            break;
        }
        case AI::AINodeParamType::Vec3:
        {
            glm::vec3 current = ParamOr<glm::vec3>(value, def.Default);
            if (ImGui::DragFloat3(def.Name.c_str(), &current.x, 0.05f))
            {
                value = current;
                changed = true;
            }
            break;
        }
        case AI::AINodeParamType::String:
        {
            std::string current = ParamOr<std::string>(value, def.Default);
            const std::string original = current;

            if (IsBlackboardKeyParam(def.Name))
            {
                comboOpen = ImGui::BeginCombo(def.Name.c_str(), current.c_str());
                comboID = ImGui::GetItemID();
                if (ImGui::IsItemActivated())
                {
                    m_ComboPending = CaptureSnapshot(document);
                    m_ComboPendingID = comboID;
                }

                if (comboOpen)
                {
                    for (const std::string_view key : standardBlackboardKeyNames)
                    {
                        if (ImGui::Selectable(std::string(key).c_str(), current == key))
                        {
                            current = key;
                            comboSelected = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                char custom[128];
                std::snprintf(custom, sizeof(custom), "%s", current.c_str());
                if (ImGui::InputText("Custom Key", custom, sizeof(custom)))
                    current = custom;
            }
            else
            {
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer), "%s", current.c_str());
                if (ImGui::InputText(def.Name.c_str(), buffer, sizeof(buffer)))
                    current = buffer;
            }

            if (current != original)
            {
                value = current;
                changed = true;
            }
            break;
        }
        }

        if (changed)
        {
            if (existing == node.Params.end())
                node.Params.emplace(def.Name, std::move(value));
            else
                existing->second = std::move(value);
        }

        if (comboSelected &&
            m_ComboPending && m_ComboPendingID == comboID)
        {
            tab.RecordDocumentEdit(std::move(*m_ComboPending),
                                   "Edit Node Parameter");
            m_ComboPending.reset();
            m_ComboPendingID = 0;
        }

        TrackEdit(tab, document, "Edit Node Parameter");
        ImGui::PopID();
    }

    ImGui::EndDisabled();
}

void AIEditorInspector::DrawStateRuntime(AIEditorTab& tab,
                                         const AIEditorDocument& document,
                                         const AIEditorStateRef& stateRef,
                                         const Ref<Scene>& activeScene)
{
    KBR_TRACY_FUNCTION();
    
    ImGui::SeparatorText("Runtime");

    const UUID agent = DrawAgentPicker(tab, document, activeScene);
    if (!agent.IsValid())
        return;

    const AI::AIAgentDebugSnapshot* snapshot =
        activeScene->GetAISystem().GetDebugSnapshot(agent);
    if (!snapshot || !snapshot->Valid)
    {
        ImGui::TextDisabled("No debug snapshot for this agent yet.");
        return;
    }

    const uint64_t evaluatedID = ResolveStateEditorID(document, snapshot->EvaluatedState);
    const uint64_t currentID = ResolveStateEditorID(document, snapshot->CurrentState);
    const uint64_t previousID = ResolveStateEditorID(document, snapshot->PreviousState);
    const uint64_t pendingID = ResolveStateEditorID(document, snapshot->PendingState);

    ImGui::Text("Current: %s", StateNameOrNone(document, currentID).c_str());
    ImGui::Text("Previous: %s", StateNameOrNone(document, previousID).c_str());
    ImGui::Text("Evaluated: %s", StateNameOrNone(document, evaluatedID).c_str());
    ImGui::Text("Pending: %s", StateNameOrNone(document, pendingID).c_str());
    ImGui::Text("Time in state: %.2fs", snapshot->TimeInState);
    ImGui::Text("Last task status: %s", StatusLabel(snapshot->LastTaskStatus));

    const AI::AIDocumentState* state = document.ResolveState(stateRef);
    if (!state)
        return;

    const bool isCurrent = stateRef.EditorID == currentID;

    ImGui::SeparatorText("Task Results");
    if (!isCurrent)
        ImGui::TextDisabled("This state's tasks were not evaluated this tick.");

    for (size_t i = 0; i < state->Tasks.size(); ++i)
    {
        const AI::AINodeDesc& task = state->Tasks[i];
        if (isCurrent && i < snapshot->Tasks.size())
        {
            const AI::AITaskDebugTrace& trace = snapshot->Tasks[i];
            ImGui::TextColored(StatusColor(trace.Status), "%s: %s",
                               trace.Type.c_str(), StatusLabel(trace.Status));
        }
        else
        {
            ImGui::TextDisabled("%s (not evaluated)", task.Type.c_str());
        }
    }
}

void AIEditorInspector::DrawTransitionRuntime(AIEditorTab& tab,
                                              const AIEditorDocument& document,
                                              const AIEditorTransitionRef& transitionRef,
                                              const Ref<Scene>& activeScene)
{
    KBR_TRACY_FUNCTION();
    
    ImGui::SeparatorText("Runtime Conditions");

    const UUID agent = DrawAgentPicker(tab, document, activeScene);
    if (!agent.IsValid())
        return;

    const AI::AIAgentDebugSnapshot* snapshot =
        activeScene->GetAISystem().GetDebugSnapshot(agent);
    if (!snapshot || !snapshot->Valid)
    {
        ImGui::TextDisabled("No debug snapshot for this agent yet.");
        return;
    }

    const uint64_t evaluatedID = ResolveStateEditorID(document, snapshot->EvaluatedState);
    const AIEditorStateRef* evaluatedRef = document.FindState(evaluatedID);
    if (!evaluatedRef || evaluatedRef->DocumentStateIndex != transitionRef.SourceStateIndex)
    {
        ImGui::TextDisabled("This transition was not evaluated this tick.");
        return;
    }

    const AI::AIDocumentTransition* transition = document.ResolveTransition(transitionRef);
    if (!transition)
        return;

    const AI::AITransitionDebugTrace* matchingCandidate = nullptr;
    for (const AI::AITransitionDebugTrace& candidate : snapshot->Candidates)
    {
        if (candidate.Name != transition->Name ||
            candidate.TargetStateName != transition->TargetState)
        {
            continue;
        }
        matchingCandidate = &candidate;
        break;
    }

    if (!matchingCandidate)
    {
        ImGui::TextDisabled("No condition trace for this transition this tick.");
        return;
    }

    ImGui::TextColored(
        matchingCandidate->Satisfied ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f)
                                     : ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
        "Transition %s", matchingCandidate->Satisfied ? "satisfied" : "not satisfied");

    for (size_t i = 0; i < transition->Conditions.size(); ++i)
    {
        const AI::AINodeDesc& condition = transition->Conditions[i];
        if (i < matchingCandidate->Conditions.size())
        {
            const AI::AIConditionDebugTrace& trace = matchingCandidate->Conditions[i];
            ImGui::TextColored(
                trace.Result ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f)
                             : ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "%s: %s", trace.Type.c_str(), trace.Result ? "true" : "false");
        }
        else
        {
            ImGui::TextDisabled("%s (not evaluated)", condition.Type.c_str());
        }
    }

    ImGui::TextDisabled("Evaluation stops after the first failing condition.");
}

UUID AIEditorInspector::DrawAgentPicker(AIEditorTab& tab,
                                        const AIEditorDocument& document,
                                        const Ref<Scene>& activeScene)
{
    KBR_TRACY_FUNCTION();
    
    struct AgentEntry
    {
        UUID ID = UUID::Invalid();
        std::string Label;
    };

    std::vector<AgentEntry> agents;

    const entt::registry& registry = activeScene->GetRegistry();
    const auto view = registry.view<AIStateMachineComponent, TagComponent, IDComponent>();
    for (const entt::entity handle : view)
    {
        const AIStateMachineComponent& stateMachine =
            view.get<AIStateMachineComponent>(handle);

        const bool matches = document.Source == AIEditorDocument::SourceKind::Asset
            ? stateMachine.TreeAsset == document.Asset
            : stateMachine.TreeName == document.RegistryName ||
              stateMachine.Tree == document.CompiledPreview;

        if (!matches)
            continue;

        const Entity entity(handle, activeScene.get());
        agents.push_back(AgentEntry{
            .ID = entity.GetUUID(),
            .Label = std::format("{} ({})", entity.GetName(), entity.GetUUID())
        });
    }

    if (agents.empty())
    {
        tab.RuntimeAgent = UUID::Invalid();
        ImGui::TextDisabled("No agents currently use this tree.");
        return tab.RuntimeAgent;
    }

    const bool known = std::ranges::any_of(agents, [&tab](const AgentEntry& entry) {
        return entry.ID == tab.RuntimeAgent;
    });
    if (!known)
        tab.RuntimeAgent = agents.front().ID;

    const auto current = std::ranges::find_if(agents, [&tab](const AgentEntry& entry) {
        return entry.ID == tab.RuntimeAgent;
    });
    const std::string currentLabel = current != agents.end() ? current->Label : "<none>";

    if (ImGui::BeginCombo("Agent", currentLabel.c_str()))
    {
        for (const AgentEntry& entry : agents)
        {
            if (ImGui::Selectable(entry.Label.c_str(), entry.ID == tab.RuntimeAgent))
                tab.RuntimeAgent = entry.ID;
        }
        ImGui::EndCombo();
    }

    return tab.RuntimeAgent;
}

void AIEditorInspector::TrackEdit(AIEditorTab& tab,
                                  const AIEditorDocument& document,
                                  const char* label)
{
    KBR_TRACY_FUNCTION();
    
    if (ImGui::IsItemActivated())
        m_PendingEdit = CaptureSnapshot(document);

    if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingEdit)
    {
        tab.RecordDocumentEdit(std::move(*m_PendingEdit), label);
        m_PendingEdit.reset();
    }
}

} // namespace Kerberos
