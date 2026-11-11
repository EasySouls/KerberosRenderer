#include "AIEditorPanel.hpp"

#include "AI/AIStateTreeRegistry.hpp"
#include "Windows/AIEditor/AIEditorConversions.hpp"
#include "Windows/AIEditor/AIEditorDebugView.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Profiling/TracyInstrumentation.hpp"
#include "Scene/Scene.hpp"
#include "Utils/SystemOperations.hpp"
#include "Widgets/AssetPicker.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <format>
#include <utility>

import Kerberos;

namespace Kerberos {

namespace {

bool IsEditMode(const bool isPlaying, const bool isSimulating)
{
    return !isPlaying && !isSimulating;
}

} // namespace

void AIEditorPanel::OnImGuiRender()
{
    KBR_TRACY_FUNCTION();
    
    m_Focused = false;
    if (!m_Open)
        return;

    ImGui::SetNextWindowSize(ImVec2(1200.0f, 780.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI Editor", &m_Open))
    {
        ImGui::End();
        return;
    }

    m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    DrawToolbar();
    ImGui::Separator();
    DrawTabs();

    if (m_RequestDiscardPopup)
    {
        ImGui::OpenPopup("Discard AI changes?");
        m_RequestDiscardPopup = false;
    }

    if (ImGui::BeginPopupModal("Discard AI changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("This AI document has unsaved changes.");
        ImGui::Separator();

        if (ImGui::Button("Discard"))
        {
            const int32_t tabIndex = m_PendingCloseTab;
            m_PendingCloseTab = -1;
            if (tabIndex >= 0 && tabIndex < static_cast<int32_t>(m_Tabs.size()))
                CloseTab(static_cast<size_t>(tabIndex));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_PendingCloseTab = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void AIEditorPanel::OnEvent([[maybe_unused]] Event& event)
{
}

void AIEditorPanel::SetContext(const Ref<Scene>& activeScene,
                               const bool isPlaying,
                               const bool isSimulating,
                               const bool isPaused)
{
    m_ActiveScene = activeScene;
    m_IsPlaying = isPlaying;
    m_IsSimulating = isSimulating;
    m_IsPaused = isPaused;
    
    const bool captureWanted = m_Open && (isPlaying || isSimulating) && activeScene;

    if (m_CaptureScene &&
        (!captureWanted || m_CaptureScene.get() != activeScene.get()))
    {
        m_CaptureScene->GetAISystem().SetDebugCaptureEnabled(false);
        m_CaptureScene.reset();
    }

    if (captureWanted)
    {
        activeScene->GetAISystem().SetDebugCaptureEnabled(true);
        m_CaptureScene = activeScene;
    }
}

void AIEditorPanel::SetOpen(const bool open)
{
    if (!open && m_CaptureScene)
    {
        m_CaptureScene->GetAISystem().SetDebugCaptureEnabled(false);
        m_CaptureScene.reset();
    }

    m_Open = open;
}

void AIEditorPanel::SetOnAssetSaved(std::function<void()> callback)
{
    m_OnAssetSaved = std::move(callback);
}

bool AIEditorPanel::HasUnsavedChanges() const
{
    return std::ranges::any_of(m_Tabs, [](const Owner<AIEditorTab>& tab) {
        return tab && tab->Document.Dirty;
    });
}

AIEditorTab* AIEditorPanel::GetActiveTab()
{
    if (m_ActiveTab < 0 || m_ActiveTab >= static_cast<int32_t>(m_Tabs.size()))
        return nullptr;
    return m_Tabs[static_cast<size_t>(m_ActiveTab)].get();
}

const AIEditorTab* AIEditorPanel::GetActiveTab() const
{
    if (m_ActiveTab < 0 || m_ActiveTab >= static_cast<int32_t>(m_Tabs.size()))
        return nullptr;
    return m_Tabs[static_cast<size_t>(m_ActiveTab)].get();
}

AIEditorTab* AIEditorPanel::FindAssetTab(const AssetHandle handle)
{
    const auto it = std::ranges::find_if(m_Tabs, [handle](const Owner<AIEditorTab>& tab) {
        return tab && !tab->Document.ViewOnly && tab->Document.Asset == handle;
    });
    return it != m_Tabs.end() ? it->get() : nullptr;
}

AIEditorTab* AIEditorPanel::FindRegistryTab(const std::string& name)
{
    const auto it = std::ranges::find_if(m_Tabs, [&name](const Owner<AIEditorTab>& tab) {
        return tab && tab->Document.ViewOnly && tab->Document.RegistryName == name;
    });
    return it != m_Tabs.end() ? it->get() : nullptr;
}

AIEditorTab& AIEditorPanel::AddTab(AIEditorDocument document)
{
    auto tab = CreateOwner<AIEditorTab>();
    tab->Document = std::move(document);
    tab->GraphContext = ax::NodeEditor::CreateEditor();
    tab->Validation = tab->Document.ViewOnly
        ? AIEditorValidationResult{}
        : AIEditorValidator::Validate(tab->Document);
    tab->ValidationDirty = false;

    m_Tabs.push_back(std::move(tab));
    m_ActiveTab = static_cast<int32_t>(m_Tabs.size() - 1);
    return *m_Tabs.back();
}

void AIEditorPanel::CloseTab(const size_t index)
{
    if (index >= m_Tabs.size())
        return;

    if (m_Tabs[index]->GraphContext)
    {
        ax::NodeEditor::DestroyEditor(m_Tabs[index]->GraphContext);
        m_Tabs[index]->GraphContext = nullptr;
    }

    m_Tabs.erase(m_Tabs.begin() + static_cast<std::ptrdiff_t>(index));

    if (m_Tabs.empty())
    {
        m_ActiveTab = -1;
        m_AssetPickerHandle = AssetHandle::Invalid();
    }
    else
    {
        if (static_cast<int32_t>(index) < m_ActiveTab)
            --m_ActiveTab;
        m_ActiveTab = std::min<int32_t>(m_ActiveTab, static_cast<int32_t>(m_Tabs.size() - 1));
    }
}

void AIEditorPanel::RequestCloseTab(const size_t index)
{
    if (index >= m_Tabs.size())
        return;

    if (m_Tabs[index]->Document.Dirty)
    {
        m_PendingCloseTab = static_cast<int32_t>(index);
        m_RequestDiscardPopup = true;
        return;
    }

    CloseTab(index);
}

void AIEditorPanel::CloseAllTabs()
{
    for (const Owner<AIEditorTab>& tab : m_Tabs)
    {
        if (tab && tab->GraphContext)
        {
            ax::NodeEditor::DestroyEditor(tab->GraphContext);
            tab->GraphContext = nullptr;
        }
    }

    m_Tabs.clear();
    m_ActiveTab = -1;
    m_AssetPickerHandle = AssetHandle::Invalid();
    m_PendingCloseTab = -1;
    m_RequestDiscardPopup = false;
}

bool AIEditorPanel::OpenAsset(const AssetHandle handle)
{
    if (!handle.IsValid())
        return false;

    if (AIEditorTab* existing = FindAssetTab(handle))
    {
        m_ActiveTab = static_cast<int32_t>(std::distance(m_Tabs.begin(),
            std::ranges::find_if(m_Tabs, [existing](const Owner<AIEditorTab>& tab) {
                return tab.get() == existing;
            })));
        m_AssetPickerHandle = handle;
        return true;
    }

    AIEditorDocument document;
    std::string error;
    if (!m_AssetSession.OpenAsset(handle, document, &error))
    {
        SetStatus(error, true);
        return false;
    }

    AddTab(std::move(document));
    m_AssetPickerHandle = handle;
    SetStatus("Opened AI state tree asset.", false);
    return true;
}

bool AIEditorPanel::OpenRegisteredTree(const std::string& name)
{
    KBR_TRACY_FUNCTION();
    
    const Ref<AI::AIStateTree> tree = AI::AIStateTreeRegistry::Find(name);
    if (!tree)
    {
        SetStatus("The selected registered state tree no longer exists.", true);
        return false;
    }

    if (AIEditorTab* existing = FindRegistryTab(name))
    {
        m_ActiveTab = static_cast<int32_t>(std::distance(m_Tabs.begin(),
            std::ranges::find_if(m_Tabs, [existing](const Owner<AIEditorTab>& tab) {
                return tab.get() == existing;
            })));
        m_AssetPickerHandle = AssetHandle::Invalid();
        return true;
    }

    AIEditorDocument document;
    document.Source = AI::AIStateTreeRegistry::GetDocument(name)
        ? AIEditorDocument::SourceKind::RegisteredDocument
        : AIEditorDocument::SourceKind::RegisteredCompiled;
    document.RegistryName = name;
    document.ViewOnly = true;
    document.Asset = AssetHandle::Invalid();
    document.Document = AI::AIStateTreeRegistry::GetDocument(name)
        ? *AI::AIStateTreeRegistry::GetDocument(name)
        : ProjectCompiledTree(*tree, name);
    document.Document.Name = name;
    document.SavedDocument = document.Document;
    document.CompiledPreview = tree;
    document.RebuildGraphReferences();

    AddTab(std::move(document));
    m_AssetPickerHandle = AssetHandle::Invalid();
    SetStatus("Opened a registered state tree in view-only mode.", false);
    return true;
}

bool AIEditorPanel::NewAsset()
{
    const std::string selected = FileDialog::SaveFile(
        "AI State Tree (*.kbrai)\0*.kbrai\0", "kbrai");
    if (selected.empty())
        return false;

    return CreateAssetAtPath(selected, true);
}

bool AIEditorPanel::CreateAssetAtPath(std::filesystem::path path,
                                      const bool overwriteExisting)
{
    if (path.empty())
        return false;

    path = std::filesystem::absolute(path);
    if (path.extension() != ".kbrai")
        path.replace_extension(".kbrai");

    if (!overwriteExisting && std::filesystem::exists(path))
    {
        SetStatus("An AI state tree already exists at " + path.filename().string() + ".", true);
        return false;
    }

    const AI::AIStateTreeDocument initial = MakeDefaultDocument(path.stem().string());
    AssetHandle handle = AssetHandle::Invalid();
    std::string error;
    if (!m_AssetSession.CreateAsset(path, initial, handle, &error))
    {
        SetStatus(error, true);
        return false;
    }

    AssetPicker::InvalidateCache();
    if (m_OnAssetSaved)
        m_OnAssetSaved();

    SetStatus("Created AI state tree asset: " + path.filename().string(), false);
    return OpenAsset(handle);
}

AI::AIStateTreeDocument AIEditorPanel::MakeDefaultDocument(std::string name)
{
    AI::AIStateTreeDocument document;
    document.Name = std::move(name);
    document.EntryState = "Idle";

    AI::AIDocumentState idle;
    idle.Name = "Idle";
    document.States.push_back(std::move(idle));
    return document;
}

void AIEditorPanel::DrawToolbar()
{
    KBR_TRACY_FUNCTION();
    
    const bool canEdit = IsEditMode(m_IsPlaying, m_IsSimulating);

    AssetHandle assetToOpen = m_AssetPickerHandle;
    if (AssetPicker::DrawAssetHandle("State Tree Asset", AssetType::AIStateTree, assetToOpen,
            "Search AI state trees...", false))
    {
        m_AssetPickerHandle = assetToOpen;
        if (assetToOpen.IsValid())
            OpenAsset(assetToOpen);
    }

    ImGui::SameLine();
    if (ImGui::Button("New"))
        NewAsset();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    const AIEditorTab* activeTab = GetActiveTab();
    const char* registryPreview = activeTab && activeTab->Document.ViewOnly
        ? activeTab->Document.RegistryName.c_str()
        : "<registered tree>";
    if (ImGui::BeginCombo("##RegisteredTree", registryPreview))
    {
        const std::vector<std::string> names = AI::AIStateTreeRegistry::GetNames();
        if (names.empty())
            ImGui::TextDisabled("No registered trees");

        for (const std::string& name : names)
        {
            if (ImGui::Selectable(name.c_str(), activeTab && activeTab->Document.RegistryName == name))
                OpenRegisteredTree(name);
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!canEdit || !activeTab || activeTab->Document.ViewOnly);
    if (ImGui::Button("Save"))
        SaveActiveTab();
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
        RevertActiveTab();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!activeTab || activeTab->Document.ViewOnly);
    if (ImGui::Button("Validate"))
        ValidateActiveTab();
    ImGui::EndDisabled();

    if (!m_StatusMessage.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(m_StatusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                                          : ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                           "%s", m_StatusMessage.c_str());
    }
}

void AIEditorPanel::DrawTabs()
{
    KBR_TRACY_FUNCTION();
    
    if (m_Tabs.empty())
    {
        ImGui::TextDisabled("Open a .kbrai asset or a registered tree to begin.");
        return;
    }

    int32_t closeIndex = -1;
    if (ImGui::BeginTabBar("##AIEditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
    {
        for (int32_t i = 0; i < static_cast<int32_t>(m_Tabs.size()); ++i)
        {
            AIEditorTab& tab = *m_Tabs[static_cast<size_t>(i)];
            const std::string title = std::format("{}##aiTab{}", GetTabTitle(tab), i);
            bool open = true;
            const ImGuiTabItemFlags flags = tab.Document.Dirty
                ? ImGuiTabItemFlags_UnsavedDocument
                : ImGuiTabItemFlags_None;

            if (ImGui::BeginTabItem(title.c_str(), &open, flags))
            {
                m_ActiveTab = i;
                m_AssetPickerHandle = tab.Document.ViewOnly
                    ? AssetHandle::Invalid()
                    : tab.Document.Asset;
                DrawTabContent(tab);
                ImGui::EndTabItem();
            }

            if (!open)
                closeIndex = i;
        }
        ImGui::EndTabBar();
    }

    if (closeIndex >= 0)
        RequestCloseTab(static_cast<size_t>(closeIndex));
}

void AIEditorPanel::DrawTabContent(AIEditorTab& tab)
{
    KBR_TRACY_FUNCTION();
    
    const bool canEdit = IsEditMode(m_IsPlaying, m_IsSimulating) && !tab.Document.ViewOnly;

    if (tab.Document.ViewOnly)
    {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                           "View-only registered tree (%s).",
                           tab.Document.Source == AIEditorDocument::SourceKind::RegisteredCompiled
                               ? "compiled"
                               : "document");
    }
    else if (!canEdit)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "Read-only while Play/Simulate is active. Return to Edit mode to save.");
    }

    ImGui::Text("Name: %s", tab.Document.Document.Name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("Entry: %s", tab.Document.Document.EntryState.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("States: %zu | Transitions: %zu",
                        tab.Document.Document.States.size(),
                        tab.Document.Transitions.size());

    ImGui::SameLine();
    if (ImGui::Button("Frame All"))
        tab.GraphNeedsRefit = true;

    ImGui::SameLine();
    ImGui::BeginDisabled(!canEdit);
    if (ImGui::Button("Auto Layout"))
    {
        const AIEditorDocumentSnapshot before = CaptureSnapshot(tab.Document);
        tab.Document.AutoLayoutStates();
        tab.RecordDocumentEdit(before, "Auto Layout");
        tab.PositionedStateIDs.clear();
        tab.GraphNeedsRefit = true;
    }
    ImGui::EndDisabled();

    if (!tab.Document.ViewOnly && tab.ValidationDirty)
    {
        tab.Validation = AIEditorValidator::Validate(tab.Document);
        tab.ValidationDirty = false;
    }

    if (tab.Document.ViewOnly)
    {
        ImGui::TextDisabled("View-only runtime source; authored validation is not available.");
    }
    else if (tab.Validation.HasErrors())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Validation failed.");
    else if (!tab.Validation.Issues.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Validation passed with warnings.");
    else
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Validation passed.");

    ImGui::Checkbox("Show authored layout (debug)", &m_ShowDocumentPreview);
    if (m_ShowDocumentPreview)
        DrawDocumentPreview(tab);

    DrawGraph(tab, canEdit);
    DrawAITransitionHistory(tab, tab.Document, m_ActiveScene,
                            m_IsPlaying || m_IsSimulating);
    DrawValidation(tab);

}

bool AIEditorPanel::UndoActiveTab()
{
    KBR_TRACY_FUNCTION();
    
    AIEditorTab* tab = GetActiveTab();
    if (!tab || tab->Document.ViewOnly || !IsEditMode(m_IsPlaying, m_IsSimulating) ||
        !tab->UndoRedo.CanUndo())
    {
        return false;
    }

    tab->UndoRedo.Undo();
    tab->ResetGraphSession();
    return true;
}

bool AIEditorPanel::RedoActiveTab()
{
    KBR_TRACY_FUNCTION();
    
    AIEditorTab* tab = GetActiveTab();
    if (!tab || tab->Document.ViewOnly || !IsEditMode(m_IsPlaying, m_IsSimulating) ||
        !tab->UndoRedo.CanRedo())
    {
        return false;
    }

    tab->UndoRedo.Redo();
    tab->ResetGraphSession();
    return true;
}

void AIEditorPanel::DrawGraph(AIEditorTab& tab, const bool canEdit)
{
    KBR_TRACY_FUNCTION();

    const ImGuiStyle& style = ImGui::GetStyle();
    constexpr float inspectorWidth = 380.0f;
    constexpr float height = 520.0f;
    
    const float graphWidth = std::max(120.0f,
        ImGui::GetContentRegionAvail().x - inspectorWidth - style.ItemSpacing.x);
    
    const bool running = m_IsPlaying || m_IsSimulating;

    const AI::AIAgentDebugSnapshot* runtimeSnapshot = nullptr;
    if (running && m_ActiveScene)
    {
        ImGui::TextDisabled("Debug Agent");
        ImGui::SameLine();
        DrawAIAgentPicker(tab, tab.Document, m_ActiveScene);

        if (tab.RuntimeAgent.IsValid())
            runtimeSnapshot = m_ActiveScene->GetAISystem().GetDebugSnapshot(tab.RuntimeAgent);
    }

    ImGui::BeginChild("##AIGraphHost", ImVec2(graphWidth, height), true);
    m_Graph.Draw(tab, canEdit, runtimeSnapshot);
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    ImGui::BeginChild("##AIInspector", ImVec2(0.0f, height), true);
    m_Inspector.Draw(tab, canEdit, m_ActiveScene, m_IsPlaying || m_IsSimulating);
    ImGui::EndChild();
}

void AIEditorPanel::DrawDocumentPreview(const AIEditorTab& tab)
{
    KBR_TRACY_FUNCTION();
    
    ImGui::Separator();
    ImGui::TextDisabled("Authored layout (debug list):");
    ImGui::BeginChild("##AIDocumentPreview", ImVec2(0.0f, 300.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (const AI::AIDocumentState& state : tab.Document.Document.States)
    {
        const bool isEntry = state.Name == tab.Document.Document.EntryState;
        ImGui::PushID(state.Name.c_str());
        if (ImGui::TreeNodeEx("##state", ImGuiTreeNodeFlags_DefaultOpen,
                "%s%s  (%.0f, %.0f)  tasks: %zu  transitions: %zu",
                state.Name.c_str(), isEntry ? "  [entry]" : "",
                state.EditorPosition.x, state.EditorPosition.y,
                state.Tasks.size(), state.Transitions.size()))
        {
            for (const AI::AINodeDesc& task : state.Tasks)
                ImGui::BulletText("Task: %s", task.Type.c_str());

            for (const AI::AIDocumentTransition& transition : state.Transitions)
            {
                ImGui::BulletText("Transition: %s -> %s (%zu conditions)",
                                  transition.Name.empty() ? "<unnamed>" : transition.Name.c_str(),
                                  transition.TargetState.c_str(),
                                  transition.Conditions.size());
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (tab.Document.Document.States.empty())
        ImGui::TextDisabled("(no states)");

    ImGui::EndChild();
}

void AIEditorPanel::DrawValidation(AIEditorTab& tab)
{
    KBR_TRACY_FUNCTION();
    
    if (tab.Validation.Issues.empty())
        return;

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (!ImGui::TreeNode("Validation Issues"))
        return;

    ImGui::BeginChild("##AIValidationIssues", ImVec2(0.0f, 140.0f), true);
    for (size_t i = 0; i < tab.Validation.Issues.size(); ++i)
    {
        const AIEditorIssue& issue = tab.Validation.Issues[i];
        const ImVec4 color = issue.Severity == AIEditorIssueSeverity::Error
            ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
            : issue.Severity == AIEditorIssueSeverity::Warning
            ? ImVec4(1.0f, 0.75f, 0.35f, 1.0f)
            : ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool clicked = ImGui::Selectable(issue.Message.c_str());
        ImGui::PopStyleColor();

        if (clicked)
        {
            if (issue.TransitionEditorID != 0)
            {
                tab.SelectedTransitionID = issue.TransitionEditorID;
                tab.SelectedStateID = 0;
                tab.PendingLinkSelection = issue.TransitionEditorID;
                tab.PendingSelectionReveal = true;
            }
            else if (issue.StateEditorID != 0)
            {
                tab.SelectedStateID = issue.StateEditorID;
                tab.SelectedTransitionID = 0;
                tab.PendingNodeSelection = issue.StateEditorID;
                tab.PendingSelectionReveal = true;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TreePop();
}

bool AIEditorPanel::SaveActiveTab()
{
    KBR_TRACY_FUNCTION();
    
    AIEditorTab* tab = GetActiveTab();
    if (!tab || tab->Document.ViewOnly)
        return false;

    tab->Validation = AIEditorValidator::Validate(tab->Document);
    tab->ValidationDirty = false;
    if (tab->Validation.HasErrors())
    {
        SetStatus("Fix validation errors before saving.", true);
        return false;
    }

    std::string error;
    if (!m_AssetSession.SaveAsset(tab->Document, &error))
    {
        SetStatus(error, true);
        return false;
    }

    tab->ValidationDirty = true;
    tab->SelectedStateID = 0;
    tab->SelectedTransitionID = 0;
    AssetPicker::InvalidateCache();
    if (m_OnAssetSaved)
        m_OnAssetSaved();
    SetStatus("Saved and reimported AI state tree asset.", false);
    return true;
}

bool AIEditorPanel::RevertActiveTab()
{
    KBR_TRACY_FUNCTION();
    
    AIEditorTab* tab = GetActiveTab();
    if (!tab || tab->Document.ViewOnly)
        return false;

    std::string error;
    if (!m_AssetSession.RevertAsset(tab->Document, &error))
    {
        SetStatus(error, true);
        return false;
    }

    tab->UndoRedo.Clear();
    tab->ResetGraphSession();
    SetStatus("Reverted AI state tree to the saved source.", false);
    return true;
}

void AIEditorPanel::ValidateActiveTab()
{
    KBR_TRACY_FUNCTION();
    
    AIEditorTab* tab = GetActiveTab();
    if (!tab)
        return;

    tab->Validation = AIEditorValidator::Validate(tab->Document);
    tab->ValidationDirty = false;

    if (tab->Validation.HasErrors())
        SetStatus("AI state tree validation failed.", true);
    else if (!tab->Validation.Issues.empty())
        SetStatus("AI state tree validation passed with warnings.", false);
    else
        SetStatus("AI state tree validation passed.", false);
}

std::string AIEditorPanel::GetTabTitle(const AIEditorTab& tab)
{
    std::string title = tab.Document.Document.Name.empty()
        ? "Untitled AI Tree"
        : tab.Document.Document.Name;
    if (tab.Document.ViewOnly)
        title += " (read-only)";
    return title;
}

void AIEditorPanel::SetStatus(std::string message, const bool error)
{
    m_StatusMessage = std::move(message);
    m_StatusIsError = error;
}

} // namespace Kerberos
