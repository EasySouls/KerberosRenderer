#pragma once

#include "Core/Core.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Project/Project.hpp"
#include "EditorWindow.hpp"
#include "../Notification/NotificationManager.hpp"

#include <imgui/imgui.h>

#include <filesystem>
#include <map>
#include <set>

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
		void ShowFileContextMenu(std::filesystem::path::iterator::reference path);

		/**
		* Context menu displayed when right-clicking on a folder in the Assets panel.
		*/
		void ShowFolderContextMenu(std::filesystem::path::iterator::reference path);

		/**
		* Context menu displayed when right-clicking on an empty area of the Assets panel.
		*/
		void ShowContextMenu(ImGuiPopupFlags popupFlags) const;

		void RefreshAssetTree();

		void ImportAssetDialog();

		void HandleAssetDragAndDrop(AssetHandle handle, const std::filesystem::path& filename);

	private:
		std::filesystem::path m_AssetsDirectory = "Assets";
		std::filesystem::path m_CurrentDirectory = std::filesystem::current_path();
		Ref<Texture2D> m_FolderIcon;
		Ref<Texture2D> m_FileIcon;

		std::map<std::filesystem::path, Ref<Texture2D>> m_AssetImages;

		struct TreeNode
		{
			std::filesystem::path Path;
			AssetHandle Handle = AssetHandle::Invalid();

			uint32_t Parent = static_cast<uint32_t>(-1);
			std::map<std::filesystem::path, uint32_t> Children;

			explicit TreeNode(std::filesystem::path path, const AssetHandle handle)
				: Path(std::move(path)), Handle(handle)
			{
			}
		};

		std::vector<TreeNode> m_AssetTreeNodes;

		enum class Mode : uint8_t
		{
			Asset = 0,
			Filesystem = 1
		};

		Mode m_Mode = Mode::Filesystem;

		NotificationManager m_NotificationManager;
	};

}
