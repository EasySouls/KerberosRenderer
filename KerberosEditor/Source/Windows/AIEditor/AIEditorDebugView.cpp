#include "AIEditorDebugView.hpp"

#include "AIEditorRuntimeView.hpp"

#include "AI/AIDebug.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace Kerberos {

namespace {

std::string UUIDPrefix(const UUID uuid)
{
    std::string text = std::format("{}", uuid);
    if (text.size() > 8)
        text.resize(8);
    return text;
}

std::string AgentLabel(const UUID agent, const Ref<Scene>& scene)
{
    const Entity entity = scene->TryGetEntityByUUID(agent);
    if (entity)
        return std::format("{} ({})", entity.GetName(), UUIDPrefix(agent));

    return UUIDPrefix(agent);
}

void SelectHistoryRow(AIEditorTab& tab, const AIEditorDocument& document,
                      const AIEditorTransitionHistoryRow& row)
{
    tab.RuntimeAgent = row.Agent;

    const size_t fromIndex = document.FindStateIndexByName(row.FromState);
    uint64_t transitionEditorID = 0;

    for (const AIEditorTransitionRef& ref : document.Transitions)
    {
        if (ref.SourceStateIndex != fromIndex)
            continue;

        const AI::AIDocumentTransition* transition = document.ResolveTransition(ref);
        if (!transition || transition->TargetState != row.ToState)
            continue;

        if (!row.TransitionName.empty() && !transition->Name.empty() &&
            transition->Name != row.TransitionName)
        {
            continue;
        }

        transitionEditorID = ref.EditorID;
        break;
    }

    if (transitionEditorID != 0)
    {
        tab.SelectedTransitionID = transitionEditorID;
        tab.SelectedStateID = 0;
        tab.PendingLinkSelection = transitionEditorID;
    }
    else if (row.ToStateEditorID != 0)
    {
        tab.SelectedStateID = row.ToStateEditorID;
        tab.SelectedTransitionID = 0;
        tab.PendingNodeSelection = row.ToStateEditorID;
    }
    else
    {
        return;
    }

    tab.PendingSelectionReveal = true;
}

} // namespace

UUID DrawAIAgentPicker(AIEditorTab& tab, const AIEditorDocument& document,
                       const Ref<Scene>& scene)
{
    struct AgentEntry
    {
        UUID ID = UUID::Invalid();
        std::string Label;
    };

    std::vector<AgentEntry> agents;

    const entt::registry& registry = scene->GetRegistry();
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

        const Entity entity(handle, scene.get());
        agents.push_back(AgentEntry{
            .ID = entity.GetUUID(),
            .Label = std::format("{} ({})", entity.GetName(), UUIDPrefix(entity.GetUUID()))
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

void DrawAITransitionHistory(AIEditorTab& tab, const AIEditorDocument& document,
                             const Ref<Scene>& scene, const bool isRunning)
{
    ImGui::SeparatorText("Transition History");

    if (!isRunning || !scene)
    {
        ImGui::TextDisabled("Available while Play or Simulate is running.");
        return;
    }

    const std::vector<AI::AITransitionEvent>& events =
        scene->GetAISystem().GetTransitionEvents();
    const std::vector<AIEditorTransitionHistoryRow> rows =
        BuildTransitionHistory(document, events);

    if (rows.empty())
    {
        ImGui::TextDisabled("No transitions captured yet.");
        return;
    }

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("##AITransitionHistory", 6, tableFlags,
                           ImVec2(0.0f, 150.0f)))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Agent", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("Transition", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("From -> To", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Prev Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const AIEditorTransitionHistoryRow& row = rows[i];
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%.2fs", row.Time);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(AgentLabel(row.Agent, scene).c_str());

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(TransitionReasonLabel(row.Reason));

        ImGui::TableSetColumnIndex(3);
        const std::string transitionLabel = row.TransitionName.empty()
            ? "<unnamed>"
            : row.TransitionName;
        if (ImGui::Selectable(transitionLabel.c_str(), false))
            SelectHistoryRow(tab, document, row);

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%s -> %s",
                    row.FromState.empty() ? "<none>" : row.FromState.c_str(),
                    row.ToState.empty() ? "<none>" : row.ToState.c_str());

        ImGui::TableSetColumnIndex(5);
        ImGui::TextDisabled("%.2fs", row.TimeInPreviousState);

        ImGui::PopID();
    }

    ImGui::EndTable();
}

} // namespace Kerberos
