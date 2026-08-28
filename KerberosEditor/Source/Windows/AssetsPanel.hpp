#pragma once

#include "Core/Core.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Material.hpp"
#include "EditorWindow.hpp"
#include "../Notification/NotificationManager.hpp"

#include <imgui/imgui.h>

#include <filesystem>
#include <map>
#include <unordered_map>

namespace Kerberos
{
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
		void ShowContextMenu(ImGuiPopupFlags popupFlags) const;

		void RefreshAssetTree();

		void ImportAssetDialog();

		static void HandleAssetDragAndDrop(AssetHandle handle, const std::filesystem::path& filename);

		void OpenMaterialEditor(const std::filesystem::path& materialPath);
		void RenderMaterialEditors();
		static void DrawMaterialTextureField(const char* label, Ref<Texture2D>& texture);

		std::filesystem::path GetRelativePath(const std::filesystem::path& absolutePath) const;

		enum class MaterialPreviewMesh : uint8_t
		{
			Cube = 0,
			Sphere = 1
		};

		struct MaterialEditorState
		{
			std::filesystem::path Filepath;
			Ref<Material> WorkingCopy;
			MaterialPreviewMesh PreviewMesh = MaterialPreviewMesh::Sphere;
			bool Open = true;
		};

	private:
		std::filesystem::path m_AssetsDirectory = "Assets";
		std::filesystem::path m_CurrentDirectory = std::filesystem::current_path();
		Ref<Texture2D> m_FolderIcon;
		Ref<Texture2D> m_FileIcon;

		std::map<std::filesystem::path, Ref<Texture2D>> m_AssetImages;

		struct ContentItem
		{
			std::filesystem::path Path;
			AssetHandle Handle = AssetHandle::Invalid();
			bool IsDirectory = false;
		};

		std::vector<ContentItem> m_ContentItems;

		std::unordered_map<std::string, MaterialEditorState> m_OpenMaterialEditors;

		NotificationManager m_NotificationManager;
	};

}
