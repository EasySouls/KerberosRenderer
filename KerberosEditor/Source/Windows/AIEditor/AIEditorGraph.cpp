#include "AIEditorGraph.hpp"
#include "AIEditorDocumentCommand.hpp"
#include "AIEditorGraphIds.hpp"
#include "AIEditorRuntimeView.hpp"

#include <glm/gtc/epsilon.hpp>
#include <imgui/imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ed = ax::NodeEditor;

namespace Kerberos {

namespace {

const char* TaskStatusLabel(const AI::TaskStatus status)
{
    switch (status)
    {
    case AI::TaskStatus::Running:   return "Running";
    case AI::TaskStatus::Succeeded: return "Succeeded";
    case AI::TaskStatus::Failed:    return "Failed";
    }
    return "Unknown";
}

ImVec4 TaskStatusColor(const AI::TaskStatus status)
{
    switch (status)
    {
    case AI::TaskStatus::Running:   return ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
    case AI::TaskStatus::Succeeded: return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
    case AI::TaskStatus::Failed:    return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    }
    return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
}

} // namespace

void AIEditorGraph::Draw(AIEditorTab& tab, const bool canEdit,
                         const AI::AIAgentDebugSnapshot* runtimeSnapshot)
{
    if (!tab.GraphContext)
        return;
    
    m_NodeToState.clear();
    m_PinRoles.clear();
    m_LinkToTransition.clear();
    m_ErrorStateIDs.clear();
    m_WarningStateIDs.clear();
    m_ErrorTransitionIDs.clear();
    m_WarningTransitionIDs.clear();

    if (!tab.Document.ViewOnly)
    {
        for (const AIEditorIssue& issue : tab.Validation.Issues)
        {
            const bool isError = issue.Severity == AIEditorIssueSeverity::Error;
            if (issue.StateEditorID != 0)
            {
                (isError ? m_ErrorStateIDs : m_WarningStateIDs).insert(issue.StateEditorID);
            }
            if (issue.TransitionEditorID != 0)
            {
                (isError ? m_ErrorTransitionIDs : m_WarningTransitionIDs)
                    .insert(issue.TransitionEditorID);
            }
        }
    }
    
    AIEditorDocument& document = tab.Document;
    
    ed::SetCurrentEditor(tab.GraphContext);
    ed::EnableShortcuts(canEdit);
    
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.5f);
    ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
    
    ed::Begin("AI State Tree", ImVec2(0.0f, 0.0f));

    if (tab.GraphNeedsSelectionClear)
    {
        ed::ClearSelection();
        tab.GraphNeedsSelectionClear = false;
    }
    
    for (AIEditorStateRef& stateRef : document.States)
    {
        DrawStateNode(tab, document, stateRef, canEdit, runtimeSnapshot);
    }
    
    DrawLinks(document, runtimeSnapshot);
    ApplyPendingSelection(tab);
    HandleCreate(tab, document, canEdit);
    HandleDelete(tab, document, canEdit);
    HandleSelection(tab);
    HandleContextMenus(tab, document, canEdit);
    
    if (tab.GraphNeedsRefit)
    {
        ed::NavigateToContent();
        tab.GraphNeedsRefit = false;
    }
    
    ed::End();
    
    ed::PopStyleColor();
    ed::PopStyleVar(2);
    ed::SetCurrentEditor(nullptr);

    RecordPositionEditIfFinished(tab);
}

void AIEditorGraph::DrawStateNode(AIEditorTab& tab,
    AIEditorDocument& document,
    const AIEditorStateRef& stateRef,
    const bool canEdit,
    const AI::AIAgentDebugSnapshot* runtimeSnapshot)
{
    AI::AIDocumentState* state = document.ResolveState(stateRef);
    if (!state)
        return;
    
    const ed::NodeId nodeId{ AIEditorGraphId::MakeStateNodeId(stateRef.EditorID) };
    const ed::PinId inputPin{ AIEditorGraphId::MakeStateInputPinId(stateRef.EditorID) };
    
    m_NodeToState[nodeId.Get()] = stateRef.EditorID;
    m_PinRoles[inputPin.Get()] = PinRole{
        .Kind = PinRoleKind::StateInput,
        .StateEditorID = stateRef.EditorID,
    };
    
    const bool isEntry = document.Document.EntryState == state->Name;

    AIEditorStateHighlight highlight;
    if (runtimeSnapshot && runtimeSnapshot->Valid)
        highlight = ResolveStateHighlight(document, *runtimeSnapshot, stateRef.EditorID);

    ImVec4 borderColor = isEntry ? ImVec4(0.35f, 0.75f, 1.0f, 1.0f)
                                 : ImVec4(0.28f, 0.28f, 0.32f, 1.0f);
    if (highlight.Current)
        borderColor = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
    else if (highlight.Pending)
        borderColor = ImVec4(1.0f, 0.72f, 0.30f, 1.0f);
    else if (highlight.Previous)
        borderColor = ImVec4(0.35f, 0.62f, 1.0f, 1.0f);
    else if (highlight.Evaluated)
        borderColor = ImVec4(0.85f, 0.85f, 0.90f, 1.0f);

    if (m_ErrorStateIDs.contains(stateRef.EditorID))
        borderColor = ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
    else if (m_WarningStateIDs.contains(stateRef.EditorID))
        borderColor = ImVec4(1.0f, 0.72f, 0.30f, 1.0f);

    ed::PushStyleColor(ed::StyleColor_NodeBorder, borderColor);

    ed::BeginNode(nodeId);
    ImGui::PushID(nodeId.AsPointer());
    
    ImGui::TextUnformatted(state->Name.c_str());

    std::string markers;
    if (isEntry)
        markers += " [entry]";
    if (highlight.Current)
        markers += " [current]";
    if (highlight.Pending)
        markers += " [pending]";
    if (highlight.Previous)
        markers += " [previous]";
    if (highlight.Evaluated)
        markers += " [evaluated]";
    if (!markers.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", markers.c_str());
    }
    ImGui::Separator();
    
    ed::BeginPin(inputPin, ed::PinKind::Input);
    ImGui::TextUnformatted("In");
    ed::EndPin();
    
    if (!state->Tasks.empty())
    {
        ImGui::Separator();
        for (size_t i = 0; i < state->Tasks.size(); ++i)
        {
            const AI::AINodeDesc& task = state->Tasks[i];
            if (runtimeSnapshot && runtimeSnapshot->Valid && highlight.Current)
            {
                if (i < runtimeSnapshot->Tasks.size())
                {
                    const AI::AITaskDebugTrace& trace = runtimeSnapshot->Tasks[i];
                    ImGui::TextColored(TaskStatusColor(trace.Status), "%s: %s",
                                       task.Type.c_str(), TaskStatusLabel(trace.Status));
                }
                else
                {
                    ImGui::TextDisabled("%s (not evaluated)", task.Type.c_str());
                }
            }
            else
            {
                ImGui::BulletText("Task: %s", task.Type.c_str());
            }
        }
    }
    
    ImGui::Separator();
    
    for (const AIEditorTransitionRef& transitionRef : document.Transitions)
    {
        if (transitionRef.SourceStateIndex != stateRef.DocumentStateIndex)
            continue;
        
        const AI::AIDocumentTransition* transition = document.ResolveTransition(transitionRef);
        if (!transition)
            continue;
        
        const ed::PinId outputPin{ AIEditorGraphId::MakeTransitionOutputPinId(transitionRef.EditorID) };
        
        m_PinRoles[outputPin.Get()] = PinRole{
            .Kind = PinRoleKind::TransitionOutput,
            .StateEditorID = stateRef.EditorID,
            .TransitionEditorID = transitionRef.EditorID,
        };
        
        ed::BeginPin(outputPin, ed::PinKind::Output);
        ImGui::TextUnformatted(transition->Name.empty()
                                   ? "<unnamed>"
                                   : transition->Name.c_str());
        ed::EndPin();
    }
    
    const ed::PinId newTransitionPin{ AIEditorGraphId::MakeNewTransitionPinId(stateRef.EditorID) };
    m_PinRoles[newTransitionPin.Get()] = PinRole{
        .Kind = PinRoleKind::NewTransition,
        .StateEditorID = stateRef.EditorID
    };

    ed::BeginPin(newTransitionPin, ed::PinKind::Output);
    ImGui::TextDisabled("+ Transition");
    ed::EndPin();

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleColor();
    
    // Position newly submitted nodes once.
    // Do not set the position every frame, or user drags will be reverted.
    if (tab.PositionedStateIDs.insert(stateRef.EditorID).second)
    {
        ed::SetNodePosition(nodeId, ImVec2(state->EditorPosition.x, state->EditorPosition.y));
    }
    else if (canEdit)
    {
        const ImVec2 position = ed::GetNodePosition(nodeId);
        constexpr float epsilon = std::numeric_limits<float>::epsilon();
        if (glm::epsilonNotEqual(position.x, state->EditorPosition.x, epsilon) ||
            glm::epsilonNotEqual(position.y, state->EditorPosition.y, epsilon))
        {
            if (!m_PendingPositionEdit)
                m_PendingPositionEdit = CaptureSnapshot(document);

            state->EditorPosition = glm::vec2(position.x, position.y);
            tab.Document.Dirty = true;
        }
    }
}

void AIEditorGraph::DrawLinks(const AIEditorDocument& document,
                              const AI::AIAgentDebugSnapshot* runtimeSnapshot)
{
    for (const AIEditorTransitionRef transitionRef : document.Transitions)
    {
        const AI::AIDocumentTransition* transition = document.ResolveTransition(transitionRef);
        if (!transition)
            continue;
        
        const size_t targetIndex = document.FindStateIndexByName(transition->TargetState);
        if (targetIndex >= document.Document.States.size())
            continue;
        
        const AIEditorStateRef* targetRef = FindStateRefByIndex(document, targetIndex);
        if (!targetRef)
            continue;
        
        const ed::LinkId linkId{ AIEditorGraphId::MakeTransitionLinkId(transitionRef.EditorID) };
        const ed::PinId startPin{ AIEditorGraphId::MakeTransitionOutputPinId(transitionRef.EditorID) };
        const ed::PinId endPin{ AIEditorGraphId::MakeStateInputPinId(targetRef->EditorID) };
        
        m_LinkToTransition[linkId.Get()] = transitionRef.EditorID;
        
        const bool selected = ed::IsLinkSelected(linkId);
        const bool error = m_ErrorTransitionIDs.contains(transitionRef.EditorID);
        const bool warning = m_WarningTransitionIDs.contains(transitionRef.EditorID);

        AIEditorLinkHighlight highlight;
        if (runtimeSnapshot && runtimeSnapshot->Valid)
            highlight = ResolveLinkHighlight(document, *runtimeSnapshot, transitionRef);

        ImVec4 color = selected
            ? ImVec4(0.45f, 0.85f, 1.0f, 1.0f)
            : ImVec4(0.55f, 0.55f, 0.6f, 1.0f);
        float thickness = selected ? 3.0f : 2.0f;

        if (highlight.Evaluated)
        {
            color = highlight.Satisfied
                ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                : ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
            thickness = highlight.Winner ? 4.5f : 2.5f;
        }

        if (error)
        {
            color = ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
            thickness = std::max(thickness, 3.0f);
        }
        else if (warning && !highlight.Evaluated)
        {
            color = ImVec4(1.0f, 0.72f, 0.30f, 1.0f);
        }

        if (selected)
        {
            color = ImVec4(0.45f, 0.85f, 1.0f, 1.0f);
            thickness = std::max(thickness, 3.5f);
        }
        
        ed::Link(linkId, startPin, endPin, color, thickness);

        if (ed::GetHoveredLink() == linkId)
        {
            const size_t conditionCount = transition->Conditions.size();
            ImGui::SetTooltip("%s -> %s\nConditions: %zu%s",
                              transition->Name.empty() ? "<unnamed>" : transition->Name.c_str(),
                              transition->TargetState.c_str(),
                              conditionCount,
                              error ? "\nValidation error" : "");
        }
    }
}

void AIEditorGraph::HandleCreate(AIEditorTab& tab, AIEditorDocument& document, const bool canEdit)
{
    // BeginCreate marks the create action active even when it returns false
    // because no link drag is in progress. EndCreate is therefore required on
    // every path, otherwise the next BeginCreate hits an assertion.
    if (ed::BeginCreate())
        HandleNewLink(tab, document, canEdit);
    ed::EndCreate();
}

void AIEditorGraph::HandleNewLink(AIEditorTab& tab, AIEditorDocument& document, const bool canEdit)
{
    ed::PinId startId;
    ed::PinId endId;
    
    if (!ed::QueryNewLink(&startId, &endId))
        return;
    
    const auto startIt = m_PinRoles.find(startId.Get());
    const auto endIt = m_PinRoles.find(endId.Get());
    
    if (!canEdit || startIt == m_PinRoles.end() || endIt == m_PinRoles.end())
    {
        ed::RejectNewItem();
        return;
    }
    
    // QueryNewLink reports the pin the drag started from first and the pin it
    // is over second. Select the source/target roles instead of swapping the
    // stored PinRole values, which would corrupt the pin table.
    const PinRole* source = &startIt->second;
    const PinRole* target = &endIt->second;
    
    if (source->Kind == PinRoleKind::StateInput && target->Kind != PinRoleKind::StateInput)
        std::swap(source, target);
    
    const bool sourceIsTransition = source->Kind == PinRoleKind::TransitionOutput ||
                                    source->Kind == PinRoleKind::NewTransition;
    if (!sourceIsTransition || target->Kind != PinRoleKind::StateInput)
    {
        ed::RejectNewItem();
        return;
    }
    
    const AIEditorStateRef* targetStateRef = document.FindState(target->StateEditorID);
    const AI::AIDocumentState* targetState = targetStateRef
        ? document.ResolveState(*targetStateRef)
        : nullptr;
    if (!targetState)
    {
        ed::RejectNewItem();
        return;
    }
    
    if (source->Kind == PinRoleKind::NewTransition)
    {
        const AIEditorStateRef* sourceStateRef = document.FindState(source->StateEditorID);
        AI::AIDocumentState* sourceState = sourceStateRef
            ? document.ResolveState(*sourceStateRef)
            : nullptr;
        if (!sourceStateRef || !sourceState)
        {
            ed::RejectNewItem();
            return;
        }
        
        if (ed::AcceptNewItem())
        {
            const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
            const std::string transitionName =
                document.GenerateUniqueTransitionName(*sourceState);
            const uint64_t newTransitionId = document.AddTransition(
                sourceStateRef->DocumentStateIndex, targetState->Name, transitionName);
            tab.RecordDocumentEdit(before, "Add Transition");
            tab.SelectedTransitionID = newTransitionId;
        }
        
        return;
    }
    
    AIEditorTransitionRef* transitionRef = document.FindTransition(source->TransitionEditorID);
    AI::AIDocumentTransition* transition = transitionRef
        ? document.ResolveTransition(*transitionRef)
        : nullptr;
    if (!transition)
    {
        ed::RejectNewItem();
        return;
    }
    
    if (ed::AcceptNewItem())
    {
        if (transition->TargetState != targetState->Name)
        {
            const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
            transition->TargetState = targetState->Name;
            tab.RecordDocumentEdit(before, "Retarget Transition");
        }

        tab.SelectedTransitionID = transitionRef->EditorID;
    }
}

void AIEditorGraph::HandleDelete(AIEditorTab& tab, AIEditorDocument& document, const bool canEdit)
{
    if (!ed::BeginDelete())
        return;

    // Capture document indices before mutating. Removing one item rebuilds the
    // session references, so a second editor ID from this batch would resolve
    // to a different state or transition and delete the wrong item.
    std::vector<std::pair<size_t, size_t>> deletedTransitions;
    std::vector<size_t> deletedStates;

    ed::LinkId linkId;
    while (ed::QueryDeletedLink(&linkId))
    {
        const auto it = m_LinkToTransition.find(linkId.Get());
        const AIEditorTransitionRef* transitionRef = it != m_LinkToTransition.end()
            ? document.FindTransition(it->second)
            : nullptr;

        if (!canEdit || !transitionRef)
        {
            ed::RejectDeletedItem();
            continue;
        }

        if (ed::AcceptDeletedItem())
        {
            deletedTransitions.emplace_back(transitionRef->SourceStateIndex,
                                            transitionRef->TransitionIndex);
        }
    }

    ed::NodeId nodeId;
    while (ed::QueryDeletedNode(&nodeId))
    {
        const auto it = m_NodeToState.find(nodeId.Get());
        const AIEditorStateRef* stateRef = it != m_NodeToState.end()
            ? document.FindState(it->second)
            : nullptr;

        if (!canEdit || !stateRef)
        {
            ed::RejectDeletedItem();
            continue;
        }

        if (ed::AcceptDeletedItem())
            deletedStates.push_back(stateRef->DocumentStateIndex);
    }

    ed::EndDelete();

    if (deletedTransitions.empty() && deletedStates.empty())
        return;

    const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
    document.RemoveTransitions(deletedTransitions);
    document.RemoveStates(deletedStates);

    std::string name;
    if (!deletedStates.empty() && !deletedTransitions.empty())
        name = "Delete Selection";
    else if (!deletedStates.empty())
        name = deletedStates.size() == 1 ? "Delete State" : "Delete States";
    else
        name = deletedTransitions.size() == 1 ? "Delete Transition" : "Delete Transitions";

    tab.RecordDocumentEdit(before, std::move(name));
    tab.ResetGraphSession();
}

void AIEditorGraph::HandleSelection(AIEditorTab& tab)
{
    const int count = ed::GetSelectedObjectCount();
    
    std::vector<ed::NodeId> nodes(static_cast<size_t>(count));
    std::vector<ed::LinkId> links(static_cast<size_t>(count));
    
    const size_t nodeCount = static_cast<size_t>(ed::GetSelectedNodes(nodes.data(), count));
    const size_t linkCount = static_cast<size_t>(ed::GetSelectedLinks(links.data(), count));
    
    nodes.resize(nodeCount);
    links.resize(linkCount);
    
    tab.SelectedStateID = 0;
    tab.SelectedTransitionID = 0;
    
    if (nodes.size() == 1)
    {
        if (const auto it = m_NodeToState.find(nodes.front().Get());
            it != m_NodeToState.end())
        {
            tab.SelectedStateID = it->second;
        }
    }
    
    if (links.size() == 1)
    {
        if (const auto it = m_LinkToTransition.find(links.front().Get());
            it != m_LinkToTransition.end())
        {
            tab.SelectedTransitionID = it->second;
        }
    }
    
    // TODO: Multiple selection
}

void AIEditorGraph::HandleContextMenus(AIEditorTab& tab, 
                                       AIEditorDocument& document, 
                                       const bool canEdit)
{
    ed::Suspend();
    
    ed::NodeId contextNode;
    ed::LinkId contextLink;
    const ImVec2 mousePosition = ImGui::GetMousePos();
    
    constexpr static const char* nodeContextId = "AI Node Context";
    constexpr static const char* linkContextId = "AI Link Context";
    constexpr static const char* backgroundContextId = "AI Background Context";
    
    if (ed::ShowNodeContextMenu(&contextNode))
    {
        m_ContextNode = contextNode.Get();
        ImGui::OpenPopup(nodeContextId);
    }
    else if (ed::ShowLinkContextMenu(&contextLink))
    {
        m_ContextLink = contextLink.Get();
        ImGui::OpenPopup(linkContextId);
    }
    else if (canEdit && ed::ShowBackgroundContextMenu())
    {
        m_ContextCanvasPosition = ed::ScreenToCanvas(mousePosition);
        ImGui::OpenPopup(backgroundContextId);
    }
    
    ed::Resume();
    
    if (ImGui::BeginPopup(nodeContextId))
    {
        const auto it = m_NodeToState.find(m_ContextNode);
        if (it != m_NodeToState.end())
        {
            if (const AIEditorStateRef* stateRef = document.FindState(it->second))
            {
                if (const AI::AIDocumentState* state = document.ResolveState(*stateRef))
                {
                    ImGui::TextDisabled("%s", state->Name.c_str());
                    ImGui::Separator();

                    if (canEdit && ImGui::MenuItem("Set as Entry State", nullptr,
                                                   document.Document.EntryState == state->Name))
                    {
                        if (document.Document.EntryState != state->Name)
                        {
                            const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
                            document.Document.EntryState = state->Name;
                            tab.RecordDocumentEdit(before, "Set Entry State");
                        }
                    }

                    if (canEdit && ImGui::MenuItem("Rename State..."))
                    {
                        m_RenameStateID = it->second;
                        std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer),
                                      "%s", state->Name.c_str());
                        m_OpenRenamePopup = true;
                    }

                    if (canEdit && ImGui::MenuItem("Duplicate State"))
                    {
                        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
                        const uint64_t newID = document.DuplicateState(it->second);
                        if (newID != 0)
                        {
                            tab.RecordDocumentEdit(before, "Duplicate State");
                            tab.SelectedStateID = newID;
                            tab.SelectedTransitionID = 0;
                            tab.PendingNodeSelection = newID;
                            tab.PendingSelectionReveal = true;
                        }
                    }

                    if (canEdit && ImGui::MenuItem("Delete State"))
                    {
                        const size_t stateIndex = stateRef->DocumentStateIndex;
                        const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
                        document.RemoveStates(std::span<const size_t>(&stateIndex, 1));
                        tab.RecordDocumentEdit(before, "Delete State");
                        tab.ResetGraphSession();
                    }
                }
            }
        }
        ImGui::EndPopup();
    }
    
    if (ImGui::BeginPopup(linkContextId))
    {
        const auto it = m_LinkToTransition.find(m_ContextLink);
        if (it != m_LinkToTransition.end())
        {
            if (ImGui::MenuItem("Edit Transition"))
            {
                tab.SelectedTransitionID = it->second;
                tab.SelectedStateID = 0;
                tab.PendingLinkSelection = it->second;
                tab.PendingSelectionReveal = true;
            }

            if (canEdit && ImGui::MenuItem("Delete Transition"))
            {
                if (const AIEditorTransitionRef* transitionRef = document.FindTransition(it->second))
                {
                    const std::pair indices{
                        transitionRef->SourceStateIndex, transitionRef->TransitionIndex
                    };
                    const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
                    document.RemoveTransitions(
                        std::span<const std::pair<size_t, size_t>>(&indices, 1));
                    tab.RecordDocumentEdit(before, "Delete Transition");
                    tab.ResetGraphSession();
                }
            }
        }
        ImGui::EndPopup();
    }
    
    if (ImGui::BeginPopup(backgroundContextId))
    {
        if (ImGui::MenuItem("Add State"))
        {
            const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
            const uint64_t id = document.AddState(
                document.GenerateUniqueStateName("New State"),
                glm::vec2(m_ContextCanvasPosition.x, m_ContextCanvasPosition.y));
            tab.RecordDocumentEdit(before, "Add State");
            tab.SelectedStateID = id;
            tab.SelectedTransitionID = 0;
        }
        ImGui::EndPopup();
    }

    if (m_OpenRenamePopup)
    {
        ImGui::OpenPopup("Rename AI State");
        m_OpenRenamePopup = false;
    }

    if (ImGui::BeginPopup("Rename AI State"))
    {
        ImGui::TextUnformatted("Rename State");
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("##AIRename", m_RenameBuffer, sizeof(m_RenameBuffer));

        if (ImGui::Button("Rename"))
        {
            if (m_RenameBuffer[0] != '\0' && document.FindState(m_RenameStateID))
            {
                const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
                document.RenameState(m_RenameStateID, m_RenameBuffer);
                tab.RecordDocumentEdit(before, "Rename State");
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void AIEditorGraph::ApplyPendingSelection(AIEditorTab& tab)
{
    if (!tab.PendingNodeSelection && !tab.PendingLinkSelection)
        return;
    
    ed::ClearSelection();
    
    if (tab.PendingNodeSelection)
    {
        const ed::NodeId nodeId{ AIEditorGraphId::MakeStateNodeId(tab.PendingNodeSelection) };
        ed::SelectNode(nodeId, false);
    }
    
    if (tab.PendingLinkSelection)
    {
        const ed::LinkId linkId{ AIEditorGraphId::MakeTransitionLinkId(tab.PendingLinkSelection) };
        ed::SelectLink(linkId, false);
    }
    
    if (tab.PendingSelectionReveal)
    {
        ed::NavigateToSelection(false);
    }
    
    tab.PendingNodeSelection = 0;
    tab.PendingLinkSelection = 0;
    tab.PendingSelectionReveal = false;
}

void AIEditorGraph::RecordPositionEditIfFinished(AIEditorTab& tab)
{
    if (!m_PendingPositionEdit || ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return;

    bool changed = false;
    const std::vector<AI::AIDocumentState>& before = m_PendingPositionEdit->Document.States;
    const std::vector<AI::AIDocumentState>& after = tab.Document.Document.States;
    const size_t count = std::min(before.size(), after.size());

    for (size_t i = 0; i < count; ++i)
    {
        if (before[i].EditorPosition.x != after[i].EditorPosition.x ||
            before[i].EditorPosition.y != after[i].EditorPosition.y)
        {
            changed = true;
            break;
        }
    }

    if (changed)
        tab.RecordDocumentEdit(std::move(*m_PendingPositionEdit), "Move Nodes");

    m_PendingPositionEdit.reset();
}

const AIEditorStateRef* AIEditorGraph::FindStateRefByIndex(const AIEditorDocument& document, const size_t index)
{
    const auto it = std::ranges::find(document.States, index,
                                     &AIEditorStateRef::DocumentStateIndex);
    return it != document.States.end() ? &*it : nullptr;
}

}