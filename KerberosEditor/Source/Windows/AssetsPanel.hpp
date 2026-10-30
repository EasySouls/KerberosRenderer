#pragma once

#include "Core/Core.hpp"
#include "Assets/Asset.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Material.hpp"
#include "EditorWindow.hpp"
#include "../Notification/NotificationManager.hpp"

#include <imgui/imgui.h>

#include <filesystem>
#include <vector>
#include <map>
#include <unordered_map>

namespace Kerberos
{
	struct AssetMetadata;

    struct AssetSelection
    {
        AssetHandle Handle = AssetHandle::Invalid();
        std::filesystem::path Path;
        AssetType Type = AssetType::Texture2D;
    };
	
	class AssetsPanel : public EditorWindow
	{
	public:
		explicit AssetsPanel(NotificationManager notificationManager);
		~AssetsPanel() override = default;

		AssetsPanel(const AssetsPanel& other) = default;
		AssetsPanel(AssetsPanel&& other) noexcept = default;
		AssetsPanel& operator=(const AssetsPanel& other) = default;
		AssetsPanel& operator=(AssetsPanel&& other) noexcept = default;

		/**
		* Updates the current directory to the specified path.
		*/
		void SetCurrentDir(const std::filesystem::path& path);

		void OnEvent(Event& event) override;
		void OnImGuiRender() override;

		
        /**
         * The currently selected source asset (root), if any.
         */
        AssetSelection GetSelectedAsset() const { return m_SelectedAsset; }

        bool HasAssetSelection() const { return !m_SelectedAsset.Path.empty(); }

		void ClearSelection();

		/**
		* Requests an asset-tree refresh on the next render.
		*/
		void RequestRefresh() { m_RefreshRequested = true; }

	private:
		/**
		* Context menu displayed when right-clicking on a file in the Assets panel.
		*/
		void ShowFileContextMenu(const std::filesystem::path& path);

		/**
		* Context menu displayed when right-clicking on a folder in the Assets panel.
		*/
		void ShowFolderContextMenu(const std::filesystem::path& path);

		/**
		* Context menu displayed when right-clicking on an empty area of the Assets panel.
		*/
		void ShowContextMenu(ImGuiPopupFlags popupFlags);

		void RefreshAssetTree();

		void ImportAssetDialog();

	    void HandleAssetDragAndDrop(AssetHandle handle, const std::filesystem::path& filename, const AssetMetadata& metadata);

		void OpenMaterialEditor(const std::filesystem::path& materialPath);
		void RenderMaterialEditors();
        static void DrawMaterialTextureField(const char* label, Ref<Texture2D>& texture);

		std::filesystem::path GetRelativePath(const std::filesystem::path& path) const;

		enum class MaterialPreviewMesh : uint8_t
		{
			Cube = 0,
			Sphere = 1
		};

		struct MaterialEditorState
		{
			std::filesystem::path Filepath;
			AssetHandle Handle = AssetHandle::Invalid();
			Ref<Material> WorkingCopy;
			MaterialPreviewMesh PreviewMesh = MaterialPreviewMesh::Sphere;
			bool Open = true;
		};

	private:
		std::filesystem::path m_AssetsDirectory = "Assets";
		std::filesystem::path m_CurrentDirectory = std::filesystem::current_path();
        std::filesystem::path m_CurrentDirectoryRelativePath;

		Ref<Texture2D> m_FolderIcon;
		Ref<Texture2D> m_FileIcon;

		std::map<std::filesystem::path, Ref<Texture2D>> m_AssetImages;

		struct ContentItem
		{
			std::filesystem::path Path;
			AssetHandle Handle = AssetHandle::Invalid();
			bool IsDirectory = false;
            std::filesystem::path RelativePath;
            std::string Name;
            std::string RelativePathString;
		};

		std::vector<ContentItem> m_ContentItems;
		bool m_RefreshRequested = false;

		// Selected source asset (root), tracked for the inspector panel.
        AssetSelection m_SelectedAsset;

		// Asset-browser data is rebuilt only when the panel refreshes.
        // Root handle -> generated sub-asset handles, sorted once during refresh.
        std::map<AssetHandle, std::vector<AssetHandle>> m_SubAssetsByRoot;

        // Snapshot of the artifact existence check; avoids IsAssetHandleValid() and
        // filesystem::exists() / is_regular_file() calls during every ImGui frame.
        std::map<AssetHandle, bool> m_CachedArtifactExists;

        // Snapshot of registry metadata, so the render loop doesn't keep querying the manager.
        std::map<AssetHandle, AssetMetadata> m_MetadataByHandle;

        // Persistent search text for the asset browser.
        char m_SearchFilter[256]{};

		std::unordered_map<std::string, MaterialEditorState> m_OpenMaterialEditors;

		NotificationManager m_NotificationManager;
	};

}
