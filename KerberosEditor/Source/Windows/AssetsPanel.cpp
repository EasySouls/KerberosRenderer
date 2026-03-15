#include "AssetsPanel.hpp"

#include "AssetConstants.hpp"
#include "Logging/Log.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "Debug/Instrumentor.hpp"
#include "Project/Project.hpp"
#include "Utils/SystemOperations.hpp"

#include <imgui/imgui.h>

#include <algorithm>


namespace Kerberos
{
	AssetsPanel::AssetsPanel(NotificationManager notificationManager)
		: m_AssetsDirectory(Project::GetAssetDirectory()), m_CurrentDirectory(m_AssetsDirectory), m_NotificationManager(
			std::move(notificationManager))
	{
		m_FolderIcon = TextureImporter::ImportTexture("Assets/Editor/directory_icon.png");
		m_FileIcon = TextureImporter::ImportTexture("Assets/Editor/file_icon.png");

		m_AssetTreeNodes.emplace_back("/", AssetHandle::Invalid());

		RefreshAssetTree();
	}


	void AssetsPanel::OnImGuiRender()
	{
		ImGui::Begin("Assets");

		const auto& relativeDir = std::filesystem::relative(m_CurrentDirectory, m_AssetsDirectory);
		const std::string title = relativeDir.string() == "." ? "Assets" : "Assets" + std::string(1, std::filesystem::path::preferred_separator) + relativeDir.string();
		ImGui::Text("Current Directory: %s", title.data());

		if (m_CurrentDirectory != m_AssetsDirectory)
		{
			ImGui::SameLine();
			if (ImGui::Button("Back"))
			{
				m_CurrentDirectory = m_CurrentDirectory.parent_path();
			}
		}

		if (m_Mode == Mode::Asset)
		{
			ImGui::SameLine();
			if (ImGui::Button("Refresh"))
			{
				RefreshAssetTree();
			}

			ImGui::SameLine();
			ImportAssetDialog();
		}

		const std::string modeLabel = m_Mode == Mode::Asset ? "Assets" : "Folders";
		if (ImGui::Button(modeLabel.c_str()))
		{
			m_Mode = (m_Mode == Mode::Asset) ? Mode::Filesystem : Mode::Asset;
			RefreshAssetTree();
		}

		static float padding = 10.0f;
		static float thumbnailSize = 96.0f;
		static float cellSize = thumbnailSize + padding;

		const float panelWidth = ImGui::GetContentRegionAvail().x;
		int columns = static_cast<int>(panelWidth / cellSize);
		columns = std::max(columns, 1);

		/// Show default context menu when right-clicking on an empty space in the panel
		ShowContextMenu(ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);

		ImGui::Columns(columns, nullptr, false);

		if (m_Mode == Mode::Asset)
		{
			KBR_PROFILE_SCOPE("AssetsPanel::OnImGuiRender - Asset Mode");

			TreeNode* node = m_AssetTreeNodes.data();

			//auto currentDir = std::filesystem::relative(m_CurrentDirectory, Project::GetAssetDirectory());
			for (const auto& p : m_CurrentDirectory)
			{
				if (node->Path == m_CurrentDirectory)
					break;

				if (node->Children.contains(p))
				{
					node = &m_AssetTreeNodes[node->Children[p]];
				}

			}

			for (const auto& [item, treeNodeIndex] : node->Children)
			{
				const bool isDirectory = std::filesystem::is_directory(Project::GetAssetDirectory() / item);

				std::string itemStr = item.generic_string();

				ImGui::PushID(itemStr.c_str());
				// TODO: Get icon based on asset type

				const Ref<Texture2D> icon = isDirectory ? m_FolderIcon : m_FileIcon;
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::ImageButton(item.string().c_str(), icon->GetRendererID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });

				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Delete"))
					{
						const bool success = FileOperations::Delete((m_AssetsDirectory / item).string().c_str());
					}
					ImGui::EndPopup();
				}

				if (!isDirectory)
				{
					const AssetHandle handle = m_AssetTreeNodes[treeNodeIndex].Handle;
					HandleAssetDragAndDrop(handle, item.filename());
				}


				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (isDirectory)
						m_CurrentDirectory /= item.filename();
				}

				ImGui::TextWrapped("%s", itemStr.c_str());

				ImGui::NextColumn();

				ImGui::PopID();
			}
		}
		else
		{
			KBR_PROFILE_SCOPE("AssetsPanel::OnImGuiRender - Filesystem Mode");

			for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
			{
				const std::filesystem::path& path = entry.path();
				const auto& relativePath = std::filesystem::relative(path, m_AssetsDirectory);
				const std::string fileName = relativePath.filename().string();

				ImGui::PushID(path.string().c_str());

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));

				if (entry.is_directory())
				{
					ImGui::ImageButton(path.string().c_str(), m_FolderIcon->GetRendererID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						m_CurrentDirectory /= path.filename();
					}

					ShowFolderContextMenu(path);
				}
				else
				{
					const auto fileExtension = path.extension();
					const bool isImageFile = (fileExtension == ".png" || fileExtension == ".jpg" || fileExtension == ".jpeg");
					if (isImageFile)
					{
						if (!m_AssetImages.contains(path))
						{
							/// Load the image and store it in the map
							const std::string fullPath = path.string();
							// TODO: Load them asynchronously and show a placeholder until loaded
							m_AssetImages[path] = TextureImporter::ImportTexture(fullPath);
						}

						ImGui::ImageButton(path.string().c_str(), m_AssetImages[path]->GetRendererID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::Text("%s", fileName.c_str());
							ImGui::EndTooltip();
						}
					}
					else
					{
						ImGui::ImageButton(path.string().c_str(), m_FileIcon->GetRendererID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
					}

					ShowFileContextMenu(path);

					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
					{
						const auto itemPath = relativePath.string();

						ImGui::SetDragDropPayload(ASSET_BROWSER_ITEM, itemPath.c_str(), itemPath.size() + 1, ImGuiCond_Once);
						ImGui::Text("%s", fileName.c_str());
						ImGui::EndDragDropSource();
					}

					/// Open the file on double click
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						/// TODO: If the file is a kerberos scene, open it in the scene editor

						/// Open the file using the default application
						const bool opened = FileOperations::OpenFile(path.string().c_str());
						if (!opened)
						{
							ImGui::Text("Could not open file: %s", fileName.c_str());
						}
					}

				}
				ImGui::TextWrapped("%s", fileName.c_str());

				ImGui::NextColumn();

				ImGui::PopStyleColor();

				ImGui::PopID();
			}
		}


		ImGui::Columns(1);

		ImGui::End();
	}

	void AssetsPanel::ShowFileContextMenu(std::filesystem::path::iterator::reference path)
	{
		if (ImGui::BeginPopupContextItem("FileContextMenu"))
		{
			ImGui::TextDisabled("%s", path.string().c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Open"))
			{
				const bool success = FileOperations::OpenFile(path.string().c_str());
				if (!success)
				{
					ImGui::Text("Could not open file: %s", path.filename().string().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Delete File"))
			{
				// TODO: Add confirmation!
				if (m_AssetImages.contains(path))
				{
					m_AssetImages.erase(path); // Release texture if it was loaded
				}
				std::filesystem::remove(path);
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Import as asset"))
			{
				const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
				assetManager->ImportAsset(path);

				ImGui::CloseCurrentPopup();
			}
			// Add other file-specific menu items (e.g., Rename, Show in Explorer)
			ImGui::EndPopup();
		}
	}

	void AssetsPanel::ShowFolderContextMenu(std::filesystem::path::iterator::reference path)
	{
		if (ImGui::BeginPopupContextItem("FolderContextMenu"))
		{
			ImGui::TextDisabled("%s", path.string().c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Open"))
			{
				m_CurrentDirectory /= path.filename();
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Delete Folder"))
			{
				// TODO: Add confirmation dialog!
				std::filesystem::remove_all(path); // Use remove_all for directories
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void AssetsPanel::ShowContextMenu(const ImGuiPopupFlags popupFlags) const
	{
		if (ImGui::BeginPopupContextWindow(nullptr, popupFlags))
		{
			if (ImGui::MenuItem("New Folder"))
			{
				std::filesystem::path newFolderPath = m_CurrentDirectory / "New Folder";
				int counter = 1;
				while (std::filesystem::exists(newFolderPath))
				{
					newFolderPath = m_CurrentDirectory / ("New Folder " + std::to_string(counter++));
				}
				std::filesystem::create_directory(newFolderPath);
			}

			ImGui::EndPopup();
		}
	}

	void AssetsPanel::RefreshAssetTree()
	{
		KBR_PROFILE_FUNCTION();

		const AssetRegistry& assetRegistry = Project::GetActive()->GetEditorAssetManager()->GetAssetRegistry();
		for (const auto& [handle, metadata] : assetRegistry)
		{
			uint32_t currentNodeIndex = 0;

			for (const auto& p : metadata.Filepath)
			{
				auto it = m_AssetTreeNodes[currentNodeIndex].Children.find(p.generic_string());
				if (it != m_AssetTreeNodes[currentNodeIndex].Children.end())
				{
					currentNodeIndex = it->second;
				}
				else
				{
					TreeNode newNode(p, handle);
					newNode.Parent = currentNodeIndex;
					m_AssetTreeNodes.push_back(newNode);

					m_AssetTreeNodes[currentNodeIndex].Children[p] = static_cast<uint32_t>(m_AssetTreeNodes.size()) - 1;
					currentNodeIndex = static_cast<uint32_t>(m_AssetTreeNodes.size()) - 1;
				}

			}
		}
	}

	void AssetsPanel::ImportAssetDialog()
	{
		if (ImGui::Button("Import Asset"))
		{
			const std::string filePath = FileDialog::OpenFile("All Files (*.*)\0*.*\0");
			if (!filePath.empty())
			{
				const std::filesystem::path assetPath = std::filesystem::path(filePath);
				const bool isInsideAssets = assetPath.is_absolute() && assetPath.string().find(m_AssetsDirectory.string()) != std::string::npos;
				if (std::filesystem::exists(assetPath))
				{
					if (isInsideAssets)
					{
						Project::GetActive()->GetEditorAssetManager()->ImportAsset(assetPath);
						RefreshAssetTree();
					}
					else
					{
						KBR_CORE_ERROR("Asset must be located inside the Assets directory: {0}", assetPath.string());
						m_NotificationManager.AddNotification(
							"Asset must be located inside the Assets directory: " + assetPath.string(),
							Notification::Type::Error
						);
					}
				}
				else
				{
					KBR_CORE_ERROR("File does not exist: {0}", assetPath.string());
				}
			}
		}
	}

	void AssetsPanel::HandleAssetDragAndDrop(const AssetHandle handle, const std::filesystem::path& filename)
	{
		if (ImGui::BeginDragDropSource())
		{
			const AssetType assetType = Project::GetActive()->GetEditorAssetManager()->GetAssetType(handle);
			const std::filesystem::path extension = filename.extension();
			if (extension == ".jpg" || extension == ".png" || extension == ".svg")
			{
				ImGui::SetDragDropPayload(ASSET_BROWSER_TEXTURE, &handle, sizeof(AssetHandle), ImGuiCond_Once);
				if (const Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle))
				{
					ImGui::Image(texture->GetRendererID(), ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
				}
				else
				{
					ImGui::Text("Invalid Texture");
				}
			}
			else if (extension == ".kbrcubemap")
			{
				ImGui::SetDragDropPayload(ASSET_BROWSER_TEXTURE_CUBE, &handle, sizeof(AssetHandle), ImGuiCond_Once);
				ImGui::Text("%s", filename.string().c_str());
			}
			else if (extension == ".obj") /// TODO: Meshes should also have their own kerberos type
			{
				ImGui::SetDragDropPayload(ASSET_BROWSER_MESH, &handle, sizeof(AssetHandle), ImGuiCond_Once);
				ImGui::Text("%s", filename.string().c_str());
			}
			else if (assetType == AssetType::Sound)
			{
				ImGui::SetDragDropPayload(ASSET_BROWSER_AUDIO, &handle, sizeof(AssetHandle), ImGuiCond_Once);
				ImGui::Text("%s", filename.string().c_str());
			}

			ImGui::EndDragDropSource();
		}
	}

	void AssetsPanel::SetCurrentDir(const std::filesystem::path& path)
	{
		if (std::filesystem::exists(path) && std::filesystem::is_directory(path))
		{
			m_CurrentDirectory = path;
			return;
		}

		KBR_CORE_ERROR("Invalid directory path: {0}", path.string());
	}

}
