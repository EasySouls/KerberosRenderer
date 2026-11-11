#pragma once

#include "EditorWindow.hpp"
#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Assets/Asset.hpp"
#include "AI/AIStateTreeDocument.hpp"
#include "Windows/AIEditor/AIEditorAssetSession.hpp"
#include "Windows/AIEditor/AIEditorTab.hpp"
#include "Windows/AIEditor/AIEditorGraph.hpp"
#include "Windows/AIEditor/AIEditorInspector.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Kerberos {

class Scene;

/// Dedicated AI authoring/debug window.
class AIEditorPanel final : public EditorWindow
{
public:
    AIEditorPanel() = default;
    ~AIEditorPanel() override = default;

    void OnImGuiRender() override;
    void OnEvent(Event& event) override;

    void SetContext(const Ref<Scene>& activeScene, bool isPlaying, bool isSimulating, bool isPaused);
    void SetOpen(bool open);
    [[nodiscard]] bool IsOpen() const { return m_Open; }
    [[nodiscard]] bool IsFocused() const { return m_Open && m_Focused; }

    /// Applies the active tab's document history. 
    /// Returns true when a command was applied.
    /// View-only tabs and Play/Simulate are ignored.
    bool UndoActiveTab();
    bool RedoActiveTab();

    /// Saves and reimports the active tab. 
    /// Returns false for view-only tabs, validation errors, and failed saves.
    bool SaveActiveTab();

    bool OpenAsset(AssetHandle handle);
    bool OpenRegisteredTree(const std::string& name);
    bool NewAsset();
    void CloseAllTabs();

    [[nodiscard]] bool HasUnsavedChanges() const;

    void SetOnAssetSaved(std::function<void()> callback);

private:
    void DrawToolbar();
    void DrawTabs();
    void DrawTabContent(AIEditorTab& tab);
    void DrawGraph(AIEditorTab& tab, bool canEdit);
    void DrawValidation(AIEditorTab& tab);
    static void DrawDocumentPreview(const AIEditorTab& tab);

    bool RevertActiveTab();
    void ValidateActiveTab();

    AIEditorTab* GetActiveTab();
    const AIEditorTab* GetActiveTab() const;
    AIEditorTab* FindAssetTab(AssetHandle handle);
    AIEditorTab* FindRegistryTab(const std::string& name);

    AIEditorTab& AddTab(AIEditorDocument document);
    void CloseTab(size_t index);
    void RequestCloseTab(size_t index);

    void SetStatus(std::string message, bool error);
    static std::string GetTabTitle(const AIEditorTab& tab);

    static AI::AIStateTreeDocument MakeDefaultDocument(std::string name);

private:
    bool m_Open = false;
    bool m_Focused = false;
    Ref<Scene> m_ActiveScene;
    Ref<Scene> m_CaptureScene;
    bool m_IsPlaying = false;
    bool m_IsSimulating = false;
    bool m_IsPaused = false;

    std::vector<Owner<AIEditorTab>> m_Tabs;
    int32_t m_ActiveTab = -1;

    AIEditorAssetSession m_AssetSession;
    AssetHandle m_AssetPickerHandle = AssetHandle::Invalid();
    std::string m_StatusMessage;
    bool m_StatusIsError = false;

    int32_t m_PendingCloseTab = -1;
    bool m_RequestDiscardPopup = false;
    bool m_ShowDocumentPreview = false;

    std::function<void()> m_OnAssetSaved;
    
    AIEditorGraph m_Graph;
    AIEditorInspector m_Inspector;
};

} // namespace Kerberos
