#include "AssetsPanel.hpp"

#include "AssetConstants.hpp"
#include "Assets/Asset.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/Importers/MaterialImporter.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "Profiling/Instrumentor.hpp"
#include "Project/Project.hpp"
#include "Utils/SystemOperations.hpp"

#include <imgui/imgui.h>

#include <algorithm>

#include "VulkanContext.hpp"

import Kerberos;

namespace Kerberos
{
	namespace
	{
		AssetHandle FindAssetHandle(const std::filesystem::path& path)
		{
			const auto relative = std::filesystem::relative(path, Project::GetAssetDirectory());
			const auto& registry = Project::GetActive()->GetEditorAssetManager()->GetAssetRegistry();
			for (const auto& [handle, metadata] : registry)
				if (metadata.Filepath.lexically_normal() == relative.lexically_normal())
					return handle;
			return AssetHandle::Invalid();
		}
	}

	AssetsPanel::AssetsPanel(NotificationManager notificationManager)
		: m_AssetsDirectory(Project::GetAssetDirectory()), m_CurrentDirectory(m_AssetsDirectory), m_NotificationManager(
			std::move(notificationManager))
	{
		m_FolderIcon = TextureImporter::ImportTexture("Assets/Editor/directory_icon.png");
		m_FileIcon = TextureImporter::ImportTexture("Assets/Editor/file_icon.png");

		RefreshAssetTree();
	}


	void AssetsPanel::OnImGuiRender()
	{
		ImGui::Begin("Assets");

		//const auto& relativeDir = std::filesystem::relative(m_CurrentDirectory, m_AssetsDirectory);
		const auto& relativeDir = GetRelativePath(m_CurrentDirectory);
		const std::string title = relativeDir.string() == "." ? "Assets" : "Assets" + std::string(1, std::filesystem::path::preferred_separator) + relativeDir.string();
		ImGui::Text("Current Directory: %s", title.data());

		if (m_CurrentDirectory != m_AssetsDirectory)
		{
			ImGui::SameLine();
			if (ImGui::Button("Back"))
			{
				m_CurrentDirectory = m_CurrentDirectory.parent_path();
				RefreshAssetTree();
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Refresh"))
			RefreshAssetTree();
		ImGui::SameLine();
		ImportAssetDialog();

		static float padding = 16.0f;
		static float thumbnailSize = 64.0f;
		static float cellSize = thumbnailSize + padding;

		const float panelWidth = ImGui::GetContentRegionAvail().x;
		int columns = static_cast<int>(panelWidth / cellSize);
		columns = std::max(columns, 1);

		/// Show default context menu when right-clicking on an empty space in the panel
		ShowContextMenu(ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);

		ImGui::Columns(columns, nullptr, false);

		for (const auto& item : m_ContentItems)
		{
			const auto& path = item.Path;
			const std::string fileName = path.filename().string();
			const auto relativePath = GetRelativePath(path);

			ImGui::PushID(relativePath.generic_string().c_str());

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));

			if (item.IsDirectory)
			{
				const uint64_t rendererID = VulkanContext::Get().GetImGuiRendererID(m_FolderIcon);
				ImGui::ImageButton(fileName.c_str(), rendererID, { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_CurrentDirectory = path;
					RefreshAssetTree();
				}
				ShowFolderContextMenu(path);
			}
			else
			{
				const auto extension = path.extension().string();
				const bool isImageFile = extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
					extension == ".ktx" || extension == ".ktx2";
				Ref<Texture2D> preview = m_FileIcon;
				if (isImageFile)
				{
					if (!m_AssetImages.contains(path))
						m_AssetImages.emplace(path, TextureImporter::ImportTexture(path.string()));
					if (m_AssetImages.at(path))
						preview = m_AssetImages.at(path);
				}
				const uint64_t rendererID = VulkanContext::Get().GetImGuiRendererID(preview);
				ImGui::ImageButton(fileName.c_str(), rendererID, { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
				const AssetHandle handle = item.Handle;
				ShowFileContextMenu(path);
				if (handle.IsValid())
					HandleAssetDragAndDrop(handle, path.filename());
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if ((handle.IsValid() && AssetManager::GetAssetType(handle) == AssetType::Material) ||
						extension == ".kbrmat" || extension == ".kbrmaterial")
						OpenMaterialEditor(path);
					else if (!FileOperations::OpenFile(path.string().c_str()))
						m_NotificationManager.AddNotification("Could not open file: " + path.string(), Notification::Type::Error);
				}
			}
			ImGui::TextWrapped("%s", fileName.c_str());
			ImGui::NextColumn();
			ImGui::PopStyleColor();
			ImGui::PopID();
		}


		ImGui::Columns(1);

		ImGui::End();

		m_NotificationManager.RenderNotifications();

		RenderMaterialEditors();
	}

	void AssetsPanel::ShowFileContextMenu(const std::filesystem::path& path)
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
					m_NotificationManager.AddNotification("Could not open file: " + path.string(), Notification::Type::Error);
				}
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Reveal in Explorer"))
			{
				const bool success = FileOperations::RevealInFileExplorer(path.string().c_str());
				if (!success)
				{
					m_NotificationManager.AddNotification("Could not reveal file in explorer: " + path.string(), Notification::Type::Error);
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

	void AssetsPanel::ShowFolderContextMenu(const std::filesystem::path& path)
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
			if (ImGui::MenuItem("Reveal in Explorer"))
			{
				const bool success = FileOperations::RevealInFileExplorer(path.string().c_str());
				if (!success)
				{
					m_NotificationManager.AddNotification("Could not reveal folder in explorer: " + path.string(), Notification::Type::Error);
				}
				ImGui::CloseCurrentPopup();
			}
            constexpr const char* deleteFolderPopup = "DeleteFolderPopup";
			if (ImGui::MenuItem("Delete Folder"))
			{
                ImGui::OpenPopup(deleteFolderPopup);

				ImGui::CloseCurrentPopup();
			}
            if (ImGui::BeginPopupModal(deleteFolderPopup)) { // TODO: Not working as expected, the popup doesn't show up
                if (ImGui::Button("Confirm Delete")) {
                    std::filesystem::remove_all(path);
                }

                ImGui::EndPopup();
            }
			ImGui::EndPopup();
		}
	}

	void AssetsPanel::ShowContextMenu(const ImGuiPopupFlags popupFlags) const
	{
		if (ImGui::BeginPopupContextWindow(nullptr, popupFlags))
		{
			// Add horizontal space
			ImGui::Text("Folder");
			ImGui::Dummy(ImVec2(20, 0));
			ImGui::SameLine();
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

			ImGui::Text("Create basic asset");
			ImGui::Dummy(ImVec2(20, 0));
			ImGui::SameLine();

			// Creating basic assets
			if (ImGui::MenuItem("Material"))
			{
				const std::string materialPathStr = FileDialog::SaveFile("Kerberos Material (*.kbrmat)\0*.kbrmat\0", "kbrmat");
				const std::filesystem::path materialPath = materialPathStr;
				if (!materialPathStr.empty())
				{
					Material material;
					material.Name = materialPath.stem().string();
					material.Params.AlbedoFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
					material.Params.MetallicFactor = 0.0f;
					material.Params.RoughnessFactor = 1.0f;

					const std::filesystem::path assetPath = std::filesystem::relative(materialPath, Project::GetProjectDirectory());
					if (!MaterialImporter::SaveMaterial(assetPath, material))
					{
						Log::EditorError("Could not create material file at path: {0}", materialPathStr);
					}
					else
					{
						Project::GetActive()->GetEditorAssetManager()->ImportAsset(assetPath);
					}
				}
				else
				{
					Log::EditorError("Material creation cancelled or invalid path.");
				}

				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
   }

	void AssetsPanel::RefreshAssetTree()
	{
		KBR_PROFILE_FUNCTION();

		m_ContentItems.clear();
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(
			m_CurrentDirectory, std::filesystem::directory_options::skip_permission_denied, error))
		{
			if (error)
			{
				error.clear();
				continue;
			}
			const auto relative = std::filesystem::relative(entry.path(), m_AssetsDirectory, error);
			if (error)
			{
				error.clear();
				continue;
			}
			bool hidden = false;
			for (const auto& component : relative)
				hidden |= component.string().starts_with('.');
			if (hidden || (entry.is_directory() && (entry.path().filename() == "Cache" ||
				entry.path().filename() == "Staging")))
				continue;

			m_ContentItems.push_back({
				entry.path(),
				entry.is_regular_file() ? FindAssetHandle(entry.path()) : AssetHandle::Invalid(),
				entry.is_directory()
			});
		}
		std::ranges::sort(m_ContentItems, [](const ContentItem& left, const ContentItem& right) {
			if (left.IsDirectory != right.IsDirectory)
				return left.IsDirectory > right.IsDirectory;
			return left.Path.filename().generic_string() < right.Path.filename().generic_string();
		});
	}

	void AssetsPanel::OpenMaterialEditor(const std::filesystem::path& materialPath)
	{
		const std::filesystem::path absolutePath = std::filesystem::absolute(materialPath);
		const std::string key = absolutePath.string();

		if (m_OpenMaterialEditors.contains(key))
		{
			m_OpenMaterialEditors[key].Open = true;
			return;
		}

		const Ref<Material> material = MaterialImporter::ImportMaterial(materialPath);
		if (!material)
		{
			m_NotificationManager.AddNotification("Could not open material: " + absolutePath.string(), Notification::Type::Error);
			return;
		}

		MaterialEditorState state;
		state.Filepath = absolutePath;
		state.WorkingCopy = material;
		state.Open = true;

		m_OpenMaterialEditors.emplace(key, std::move(state));
	}

	void AssetsPanel::DrawMaterialTextureField(const char* label, Ref<Texture2D>& texture)
	{
		std::string textureLabel = "None";
		if (texture && texture->GetHandle().IsValid())
		{
			const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
			if (assetManager->IsAssetHandleValid(texture->GetHandle()))
			{
				textureLabel = assetManager->GetMetadata(texture->GetHandle()).Filepath.filename().string();
			}
		}

		ImGui::Text("%s: %s", label, textureLabel.c_str());
		ImGui::SameLine();
		const std::string buttonLabel = std::string("Set##") + label;
		if (ImGui::Button(buttonLabel.c_str()))
		{
			const std::string selectedPath = FileDialog::OpenFile("Textures (*.png;*.jpg;*.jpeg;*.ktx;*.ktx2)\0*.png;*.jpg;*.jpeg;*.ktx;*.ktx2\0");
			if (!selectedPath.empty())
			{
				const std::filesystem::path pathToImport = std::filesystem::relative(selectedPath, Project::GetProjectDirectory());

				const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
				const AssetHandle handle = assetManager->ImportAsset(pathToImport);
				if (handle.IsValid())
				{
					texture = AssetManager::GetAsset<Texture2D>(handle);
				}
			}
		}

		ImGui::SameLine();
		const std::string clearButtonLabel = std::string("Clear##") + label;
		if (ImGui::Button(clearButtonLabel.c_str()))
		{
			texture = nullptr;
		}
	}

	std::filesystem::path AssetsPanel::GetRelativePath(const std::filesystem::path& absolutePath) const
    {
		return std::filesystem::relative(absolutePath, m_AssetsDirectory);
	}

	void AssetsPanel::RenderMaterialEditors()
	{
		for (auto it = m_OpenMaterialEditors.begin(); it != m_OpenMaterialEditors.end();)
		{
			auto& [key, state] = *it;
			bool open = state.Open;
			const std::string windowTitle = std::string("Material Editor - ") + state.Filepath.filename().string() + "##" + key;

			bool beginWasCalled = false;

			if (open ? ImGui::Begin(windowTitle.c_str(), &open) : false)
			{
				beginWasCalled = true;

				char nameBuffer[256];
				strcpy_s(nameBuffer, sizeof(nameBuffer), state.WorkingCopy->Name.c_str());
				if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
				{
					state.WorkingCopy->Name = nameBuffer;
				}

				ImGui::ColorEdit4("Albedo", &state.WorkingCopy->Params.AlbedoFactor[0]);
				ImGui::DragFloat("Roughness", &state.WorkingCopy->Params.RoughnessFactor, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Metallic", &state.WorkingCopy->Params.MetallicFactor, 0.01f, 0.0f, 1.0f);
				ImGui::ColorEdit3("Emissive Color", &state.WorkingCopy->EmissiveColor[0]);
				ImGui::DragFloat("Emissive Intensity", &state.WorkingCopy->EmissiveIntensity, 1.0f, 0.0f, 1000.0f);
				state.WorkingCopy->Params.Emissive = state.WorkingCopy->EmissiveColor * state.WorkingCopy->EmissiveIntensity;
				const auto& emissive = state.WorkingCopy->Params.Emissive;
				ImGui::Text("Emissive: (%.3f, %.3f, %.3f)", emissive.x, emissive.y, emissive.z);

				ImGui::Separator();
				ImGui::Text("Preview Mesh");
				int previewMesh = static_cast<int>(state.PreviewMesh);
				ImGui::RadioButton("Sphere", &previewMesh, static_cast<int>(MaterialPreviewMesh::Sphere));
				ImGui::SameLine();
				ImGui::RadioButton("Cube", &previewMesh, static_cast<int>(MaterialPreviewMesh::Cube));
				state.PreviewMesh = static_cast<MaterialPreviewMesh>(previewMesh);
				ImGui::TextDisabled("Preview mesh selection is saved for this editor session.");

				ImGui::Separator();
				DrawMaterialTextureField("Albedo Texture", state.WorkingCopy->AlbedoTexture);
				DrawMaterialTextureField("Normal Texture", state.WorkingCopy->NormalTexture);
				DrawMaterialTextureField("Metallic Texture", state.WorkingCopy->MetallicTexture);
				DrawMaterialTextureField("Roughness Texture", state.WorkingCopy->RoughnessTexture);
				DrawMaterialTextureField("AO Texture", state.WorkingCopy->AOTexture);
				DrawMaterialTextureField("Emissive Texture", state.WorkingCopy->EmissiveTexture);
				ImGui::Separator();
				if (ImGui::Button("Save"))
				{
					const auto relPath = std::filesystem::relative(state.Filepath, Project::GetProjectDirectory());
					if (MaterialImporter::SaveMaterial(relPath, *state.WorkingCopy))
					{
						const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
						const AssetHandle materialHandle = assetManager->ImportAsset(relPath);
						if (materialHandle.IsValid())
						{
							if (const Ref<Material> loadedMaterial = AssetManager::GetAsset<Material>(materialHandle))
							{
								*loadedMaterial = *state.WorkingCopy;
								loadedMaterial->GetHandle() = materialHandle;
							}
						}

						m_NotificationManager.AddNotification("Saved material: " + state.Filepath.string(), Notification::Type::Info);
					}
					else
					{
						m_NotificationManager.AddNotification("Failed to save material: " + state.Filepath.string(), Notification::Type::Error);
					}
				}
			}

			if (open || beginWasCalled)
			{
				ImGui::End();
			}

			state.Open = open;

			if (state.Open)
			{
				++it;
			}
			else
			{
				it = m_OpenMaterialEditors.erase(it);
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
						Log::EditorError("Asset must be located inside the Assets directory: {0}", assetPath.string());
						m_NotificationManager.AddNotification(
							"Asset must be located inside the Assets directory: " + assetPath.string(),
							Notification::Type::Error
						);
					}
				}
				else
				{
					Log::EditorError("File does not exist: {0}", assetPath.string());
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

			const std::string_view assetTypeStr = AssetTypeToString(assetType);

			if (extension == ".jpg" || extension == ".png" || extension == ".svg")
			{
				ImGui::SetDragDropPayload(assetBrowserTexture, &handle, sizeof(AssetHandle), ImGuiCond_Once);
				if (const Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle))
				{
					const uint64_t textureRendererID = VulkanContext::Get().GetImGuiRendererID(texture);
					ImGui::Image(textureRendererID, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
				}
				else
				{
					ImGui::Text("Invalid Texture");
				}
			}
			else if (extension == ".kbrcubemap")
			{
				ImGui::SetDragDropPayload(assetBrowserTextureCube, &handle, sizeof(AssetHandle), ImGuiCond_Once);
			}
			else if (assetType == AssetType::Mesh || assetType == AssetType::Model)
			{
				ImGui::SetDragDropPayload(assetBrowserMesh, &handle, sizeof(AssetHandle), ImGuiCond_Once);
			}
			else if (assetType == AssetType::Sound)
			{
				ImGui::SetDragDropPayload(assetBrowserAudio, &handle, sizeof(AssetHandle), ImGuiCond_Once);
			}
			else if (assetType == AssetType::Material)
			{
				ImGui::SetDragDropPayload(assetBrowserMaterial, &handle, sizeof(AssetHandle), ImGuiCond_Once);
			}

			ImGui::Text("%s", filename.string().c_str());
			ImGui::Text("Type: %.*s", static_cast<int>(assetTypeStr.length()), assetTypeStr.data());

			ImGui::EndDragDropSource();
		}
	}

	void AssetsPanel::SetCurrentDir(const std::filesystem::path& path)
	{
		if (std::filesystem::exists(path) && std::filesystem::is_directory(path))
		{
			m_CurrentDirectory = path;
			RefreshAssetTree();
			return;
		}

		Log::EditorError("Invalid directory path: {0}", path.string());
	}

	void AssetsPanel::OnEvent([[maybe_unused]] Event& event)
	{
		
	}
}
