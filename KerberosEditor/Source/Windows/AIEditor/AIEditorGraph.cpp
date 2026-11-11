#include "AIEditorGraph.hpp"
#include "AIEditorDocumentCommand.hpp"
#include "AIEditorGraphIds.hpp"

#include <glm/gtc/epsilon.hpp>
#include <imgui/imgui.h>
#include <imgui_node_editor.h>

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ed = ax::NodeEditor;

namespace Kerberos {

void AIEditorGraph::Draw(AIEditorTab& tab, const bool canEdit)
{
    if (!tab.GraphContext)
        return;
    
    m_NodeToState.clear();
    m_PinRoles.clear();
    m_LinkToTransition.clear();
    
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
        DrawStateNode(tab, document, stateRef, canEdit);
    }
    
    DrawLinks(tab, document);
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
}

void AIEditorGraph::DrawStateNode(AIEditorTab& tab,
    AIEditorDocument& document,
    const AIEditorStateRef& stateRef,
    const bool canEdit)
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
    
    ed::PushStyleColor(ed::StyleColor_NodeBorder,
                       isEntry ? ImVec4(0.35f, 0.75f, 1.0f, 1.0f)
                               : ImVec4(0.28f, 0.28f, 0.32f, 1.0f));

    ed::BeginNode(nodeId);
    ImGui::PushID(nodeId.AsPointer());
    
    ImGui::TextUnformatted(state->Name.c_str());
    if (isEntry)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("[entry]");
    }
    ImGui::Separator();
    
    ed::BeginPin(inputPin, ed::PinKind::Input);
    ImGui::TextUnformatted("In");
    ed::EndPin();
    
    if (!state->Tasks.empty())
    {
        ImGui::Separator();
        for (const AI::AINodeDesc& task : state->Tasks)
        {
            ImGui::BulletText("Task: %s", task.Type.c_str());
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
            state->EditorPosition = glm::vec2(position.x, position.y);
            tab.Document.Dirty = true;
        }
    }
}

void AIEditorGraph::DrawLinks([[maybe_unused]] AIEditorTab& tab, const AIEditorDocument& document)
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
        const ImVec4 color = selected
            ? ImVec4(0.45f, 0.85f, 1.0f, 1.0f)
            : ImVec4(0.55f, 0.55f, 0.6f, 1.0f);
        const float thickness = selected ? 3.0f : 2.0f;
        
        ed::Link(linkId, startPin, endPin, color, thickness);
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
        if (it != m_LinkToTransition.end() && canEdit)
        {
            if (ImGui::MenuItem("Delete Transition"))
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

const AIEditorStateRef* AIEditorGraph::FindStateRefByIndex(const AIEditorDocument& document, const size_t index)
{
    const auto it = std::ranges::find(document.States, index,
                                     &AIEditorStateRef::DocumentStateIndex);
    return it != document.States.end() ? &*it : nullptr;
}

}