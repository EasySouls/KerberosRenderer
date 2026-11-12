#include "AssetsPanel.hpp"

#include "AssetConstants.hpp"
#include "Assets/Asset.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/Importers/MaterialImporter.hpp"
#include "Core/Color.hpp"
#include "Profiling/Profilers.hpp"
#include "Project/Project.hpp"
#include "Utils/SystemOperations.hpp"
#include "Widgets/AssetPicker.hpp"
#include "Widgets/ColorWidgets.hpp"
#include "Widgets/EditorIcons.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdio>
#include <string_view>

#include "VulkanContext.hpp"

#include <ranges>

import Kerberos;

namespace Kerberos {

namespace {

std::string NormalizeAssetPath(const std::filesystem::path& path)
{
    std::string value = path.lexically_normal().generic_string();
    std::ranges::transform(value, value.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::filesystem::path MakeAbsoluteLexical(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

std::filesystem::path MakeLexicallyRelative(const std::filesystem::path& path, const std::filesystem::path& root)
{
    return MakeAbsoluteLexical(path).lexically_relative(MakeAbsoluteLexical(root));
}

EditorIcon AssetIconFor(const AssetMetadata* metadata, const std::filesystem::path& path)
{
    if (metadata != nullptr)
        return EditorIcons::ForAssetType(metadata->Type);

    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".ktx" || extension == ".ktx2")
        return EditorIcon::AssetTexture2d;
    if (extension == ".kbrcubemap")
        return EditorIcon::AssetTexturecube;
    if (extension == ".fbx" || extension == ".obj")
        return EditorIcon::AssetMesh;
    if (extension == ".gltf" || extension == ".glb" || extension == ".kbrprefab")
        return EditorIcon::AssetPrefab;
    if (extension == ".kerberos")
        return EditorIcon::AssetScene;
    if (extension == ".wav")
        return EditorIcon::AssetSound;
    if (extension == ".kbrmat" || extension == ".kbrmaterial")
        return EditorIcon::AssetMaterial;
    if (extension == ".kbranim" || extension == ".kbranimation")
        return EditorIcon::AssetAnimation;
    if (extension == ".cs")
        return EditorIcon::Script;
    if (extension == ".csproj")
        return EditorIcon::Csproj;
    if (extension == ".slang" || extension == ".spv")
        return EditorIcon::Shader;
    if (extension == ".ttf" || extension == ".otf")
        return EditorIcon::Font;
    if (extension == ".meta")
        return EditorIcon::FileMeta;
    if (extension == ".kbrscene")
        return EditorIcon::SceneManifest;

    return EditorIcon::File;
}

EditorIcon StatusIconFor(const std::string& status)
{
    if (status == "Ready")
        return EditorIcon::StatusReady;
    if (status == "Missing artifact")
        return EditorIcon::StatusMissing;
    if (status == "Not imported")
        return EditorIcon::StatusNotImported;
    return EditorIcon::StatusRegistered;
}

std::string AssetDisplayName(const AssetMetadata& metadata, const AssetHandle handle)
{
    if (!metadata.LibraryPath.empty() && !metadata.LibraryPath.stem().empty())
        return metadata.LibraryPath.stem().string();
    if (!metadata.SubAssetKey.empty())
        return metadata.SubAssetKey;
    return std::format("Asset {}", static_cast<uint64_t>(handle));
}

std::string AssetStatusLabel(const AssetHandle handle,
                             const AssetMetadata* metadata,
                             const std::map<AssetHandle, bool>& artifactExists)
{
    if (!handle.IsValid() || metadata == nullptr)
        return "Not imported";
    if (metadata->LibraryPath.empty())
        return "Registered - no artifact";
    const auto it = artifactExists.find(handle);
    if (it != artifactExists.end() && it->second)
        return "Ready";
    return "Missing artifact";
}

}

	AssetsPanel::AssetsPanel(NotificationManager notificationManager)
		: m_AssetsDirectory(Project::GetAssetDirectory()), m_CurrentDirectory(m_AssetsDirectory), m_NotificationManager(
			std::move(notificationManager))
	{
	    // Normalize once. The browser always holds absolute paths after this point,
        // so it can use lexical path operations instead of filesystem::relative(), which tanks the framerate.
        m_AssetsDirectory = MakeAbsoluteLexical(m_AssetsDirectory);
        m_CurrentDirectory = m_AssetsDirectory;

		RefreshAssetTree();
	}


	void AssetsPanel::OnImGuiRender()
    {
        KBR_TRACY_FUNCTION();

        const bool windowVisible = ImGui::Begin("Assets");
        std::filesystem::path directoryToOpen;
        if (windowVisible)
        {
            const auto& relativeDir = m_CurrentDirectoryRelativePath;
            std::string title = "Assets";
            if (!relativeDir.empty() && relativeDir != ".")
                title += " / " + relativeDir.generic_string();
            ImGui::TextUnformatted(title.c_str());

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

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##AssetSearch", "Search this folder...", m_SearchFilter, sizeof(m_SearchFilter));

            ShowContextMenu(ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);

            const std::string search = [&]() {
                std::string value = m_SearchFilter;
                std::ranges::transform(
                    value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            }();

            const auto matchesSearch = [&search](const std::string& text) {
                if (search.empty())
                    return true;
                std::string normalized = text;
                std::ranges::transform(
                    normalized, normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return normalized.find(search) != std::string::npos;
            };

            if (ImGui::BeginTable("AssetBrowserTable",
                                  4,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.8f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Import status", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Sub-asset key / path", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const auto& item : m_ContentItems)
                {
                    const auto& fileName = item.Name;
                    const auto handle = item.Handle;
                    const auto metadataIt = m_MetadataByHandle.find(handle);
                    const bool registered = handle.IsValid() && metadataIt != m_MetadataByHandle.end();
                    const AssetMetadata* metadata = registered ? &metadataIt->second : nullptr;

                    bool matches = matchesSearch(fileName);
                    // Keep a source row visible when the search matches one of its generated children.
                    if (!matches && registered)
                    {
                        if (const auto children = m_SubAssetsByRoot.find(handle); children != m_SubAssetsByRoot.end())
                        {
                            for (const AssetHandle child : children->second)
                            {
                                const auto childIt = m_MetadataByHandle.find(child);
                                if (childIt == m_MetadataByHandle.end())
                                    continue;
                                const auto& childMetadata = childIt->second;
                                if (matchesSearch(AssetDisplayName(childMetadata, child)) ||
                                    matchesSearch(childMetadata.SubAssetKey))
                                {
                                    matches = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!matches)
                        continue;

                    const auto& treePathId = item.RelativePathString;
                    const auto subAssetsIt = registered ? m_SubAssetsByRoot.find(handle) : m_SubAssetsByRoot.end();
                    const bool hasSubAssets = subAssetsIt != m_SubAssetsByRoot.end() && !subAssetsIt->second.empty();
                    bool childrenOpen = false;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(treePathId.c_str());

                    if (item.IsDirectory)
                    {
                        EditorIcons::Draw(EditorIcon::Folder);
                        ImGui::SameLine(0.0f, 4.0f);
                        if (ImGui::Selectable(fileName.c_str(),
                                              false,
                                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            ClearSelection();
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            directoryToOpen = item.Path;
                        }

                        ShowFolderContextMenu(item.Path);
                    }
                    else if (hasSubAssets)
                    {
                        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth |
                                                             ImGuiTreeNodeFlags_OpenOnArrow |
                                                             ImGuiTreeNodeFlags_FramePadding;
                        EditorIcons::Draw(AssetIconFor(metadata, item.Path));
                        ImGui::SameLine(0.0f, 4.0f);
                        childrenOpen = ImGui::TreeNodeEx("##AssetTreeNode", flags, "%s", fileName.c_str());
                        if (registered && metadata)
                        {
                            HandleAssetDragAndDrop(handle, item.Path.filename(), *metadata);
                        }

                        ShowFileContextMenu(item.Path);

                        if (ImGui::IsItemClicked())
                        {
                            m_SelectedAsset = { 
                                .Handle = handle,
                                .Path = item.Path,
                                .Type = metadata ? metadata->Type : AssetType::Prefab 
                            };
                        }

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            const auto extension = item.Path.extension();
                            if (extension == ".kbrmat" || extension == ".kbrmaterial")
                            {
                                OpenMaterialEditor(item.Path);
                            }
                            else if (extension == ".kbrai" && m_OnAIStateTreeActivated)
                            {
                                m_OnAIStateTreeActivated(handle, item.Path);
                            }
                            else if (!FileOperations::OpenFile(item.Path.string().c_str()))
                            {
                                m_NotificationManager.AddNotification("Could not open file: " + item.Path.string(),
                                                                      Notification::Type::Error);
                            }
                        }
                    }
                    else
                    {
                        const bool selected = handle.IsValid() && handle == m_SelectedAsset.Handle;
                        EditorIcons::Draw(AssetIconFor(metadata, item.Path));
                        ImGui::SameLine(0.0f, 4.0f);
                        if (ImGui::Selectable(
                            fileName.c_str(),
                            selected,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            m_SelectedAsset = { .Handle = handle, .Path = item.Path, .Type = metadata ? metadata->Type : AssetType::Texture2D };
                        }

                        if (registered && metadata)
                        {
                            HandleAssetDragAndDrop(handle, item.Path.filename(), *metadata);
                        }

                        ShowFileContextMenu(item.Path);

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            const auto extension = item.Path.extension();
                            if (extension == ".kbrmat" || extension == ".kbrmaterial")
                            {
                                OpenMaterialEditor(item.Path);
                            }
                            else if (extension == ".kbrai" && m_OnAIStateTreeActivated)
                            {
                                m_OnAIStateTreeActivated(handle, item.Path);
                            }
                            else if (!FileOperations::OpenFile(item.Path.string().c_str()))
                            {
                                m_NotificationManager.AddNotification("Could not open file: " + item.Path.string(),
                                                                      Notification::Type::Error);
                            }
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    if (metadata)
                    {
                        const auto typeName = AssetTypeToString(metadata->Type);
                        ImGui::Text("%.*s", static_cast<int>(typeName.size()), typeName.data());
                    }
                    else
                    {
                        ImGui::TextDisabled(item.IsDirectory ? "Folder" : "Source file");
                    }

                    ImGui::TableSetColumnIndex(2);
                    if (item.IsDirectory)
                    {
                        ImGui::TextDisabled("-");
                    }
                    else
                    {
                        const auto status = AssetStatusLabel(handle, metadata, m_CachedArtifactExists);
                        EditorIcons::Draw(StatusIconFor(status), 0.0f);
                        ImGui::SameLine(0.0f, 4.0f);
                        if (status == "Ready")
                        {
                            ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.45f, 1.0f), "%s", status.c_str());
                        }
                        else if (status == "Missing artifact")
                        {
                            ImGui::TextColored(ImVec4(0.95f, 0.48f, 0.30f, 1.0f), "%s", status.c_str());
                        }
                        else
                        {
                            ImGui::TextDisabled("%s", status.c_str());
                        }
                    }

                    ImGui::TableSetColumnIndex(3);
                    if (metadata && !metadata->SubAssetKey.empty())
                    {
                        ImGui::TextDisabled("%s", metadata->SubAssetKey.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("%s", treePathId.c_str());
                    }

                    if (childrenOpen)
                    {
                        for (const AssetHandle childHandle : subAssetsIt->second)
                        {
                            const auto childIt = m_MetadataByHandle.find(childHandle);
                            if (childIt == m_MetadataByHandle.end())
                                continue;

                            const auto& childMetadata = childIt->second;
                            const std::string childName = AssetDisplayName(childMetadata, childHandle);
                            if (!matchesSearch(childName) && !matchesSearch(childMetadata.SubAssetKey) &&
                                !search.empty())
                                continue;

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::PushID(static_cast<int>(static_cast<uint64_t>(childHandle) & 0x7fffffff));

                            ImGui::Indent(16.0f);
                            EditorIcons::Draw(EditorIcons::ForAssetType(childMetadata.Type));
                            ImGui::SameLine(0.0f, 4.0f);
                            ImGui::Selectable(childName.c_str(),
                                              false,
                                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                            ImGui::Unindent(16.0f);
                            HandleAssetDragAndDrop(childHandle, childMetadata.LibraryPath.filename(), childMetadata);
                            if (ImGui::BeginPopupContextItem("GeneratedSubAssetContext"))
                            {
                                ImGui::TextDisabled("Generated by the source importer");
                                ImGui::Text("Key: %s", childMetadata.SubAssetKey.c_str());
                                ImGui::Text("Handle: %llu",
                                            static_cast<unsigned long long>(static_cast<uint64_t>(childHandle)));
                                if (ImGui::MenuItem("Reveal generated artifact") && !childMetadata.LibraryPath.empty())
                                {
                                    const auto artifact =
                                        childMetadata.LibraryPath.is_absolute()
                                            ? childMetadata.LibraryPath
                                            : (m_AssetsDirectory / "Cache" / childMetadata.LibraryPath);
                                    if (!FileOperations::RevealInFileExplorer(artifact.string().c_str()))
                                        m_NotificationManager.AddNotification("Could not reveal artifact: " +
                                                                                  artifact.string(),
                                                                              Notification::Type::Error);
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::TableSetColumnIndex(1);
                            const auto childTypeName = AssetTypeToString(childMetadata.Type);
                            ImGui::Text("%.*s", static_cast<int>(childTypeName.size()), childTypeName.data());
                            ImGui::TableSetColumnIndex(2);
                            const auto status = AssetStatusLabel(childHandle, &childMetadata, m_CachedArtifactExists);
                            EditorIcons::Draw(StatusIconFor(status), 0.0f);
                            ImGui::SameLine(0.0f, 4.0f);
                            if (status == "Ready")
                                ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.45f, 1.0f), "%s", status.c_str());
                            else if (status == "Missing artifact")
                                ImGui::TextColored(ImVec4(0.95f, 0.48f, 0.30f, 1.0f), "%s", status.c_str());
                            else
                                ImGui::TextDisabled("%s", status.c_str());
                            ImGui::TableSetColumnIndex(3);
                            ImGui::TextDisabled("%s", childMetadata.SubAssetKey.c_str());
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            else
            {
                ImGui::TextDisabled("No assets to display.");
            }

            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ClearSelection();
            }

            if (m_OpenCreateAITreePopup)
            {
                ImGui::OpenPopup("Create AI State Tree");
                m_OpenCreateAITreePopup = false;
            }

            if (ImGui::BeginPopup("Create AI State Tree"))
            {
                ImGui::TextUnformatted("Create AI State Tree");
                ImGui::TextDisabled("%s", m_PendingCreateAITreeFolder.string().c_str());
                ImGui::Separator();

                ImGui::SetNextItemWidth(280.0f);
                ImGui::InputText("##AITreeFileName", m_NewAITreeFileName,
                                 sizeof(m_NewAITreeFileName));

                const std::string fileName = m_NewAITreeFileName;
                const bool validName = !fileName.empty() &&
                    fileName != "." && fileName != ".." &&
                    std::filesystem::path(fileName).filename().string() == fileName;

                ImGui::BeginDisabled(!validName);
                if (ImGui::Button("Create") && m_OnCreateAIStateTree)
                {
                    std::filesystem::path path = fileName;
                    if (path.extension() != ".kbrai")
                        path.replace_extension(".kbrai");

                    m_OnCreateAIStateTree(m_PendingCreateAITreeFolder / path.filename());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }
        }
        ImGui::End();

        if (!directoryToOpen.empty())
        {
            SetCurrentDir(directoryToOpen);
        }
        else if (m_RefreshRequested)
        {
            m_RefreshRequested = false;
            RefreshAssetTree();
        }

        m_NotificationManager.RenderNotifications();
        RenderMaterialEditors();
    }

	void AssetsPanel::ClearSelection()
    {
        m_SelectedAsset = {};
    }

	void AssetsPanel::ShowFileContextMenu(const std::filesystem::path& path)
    {
        if (ImGui::BeginPopupContextItem("FileContextMenu"))
        {
            ImGui::TextDisabled("%s", path.string().c_str());
            ImGui::Separator();

            if (path.extension() == ".kbrai" && m_OnAIStateTreeActivated)
            {
                if (ImGui::MenuItem("Open in AI Editor"))
                {
                    if (const Ref<EditorAssetManager> assetManager =
                            Project::GetActive()->GetEditorAssetManager())
                    {
                        const AssetHandle handle = assetManager->ImportAsset(path);
                        if (handle.IsValid())
                            m_OnAIStateTreeActivated(handle, path);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Open"))
            {
                if (!FileOperations::OpenFile(path.string().c_str()))
                {
                    m_NotificationManager.AddNotification("Could not open file: " + path.string(),
                                                          Notification::Type::Error);
                }
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Reveal in Explorer"))
            {
                if (!FileOperations::RevealInFileExplorer(path.string().c_str()))
                {
                    m_NotificationManager.AddNotification("Could not reveal file in explorer: " + path.string(),
                                                          Notification::Type::Error);
                }
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Delete File"))
            {
                // TODO: Add confirmation
                if (m_AssetImages.contains(path))
                {
                    m_AssetImages.erase(path); // Release texture if it was loaded
                }
                std::filesystem::remove(path);
                m_RefreshRequested = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Import as asset"))
            {
                const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
                const AssetHandle handle = assetManager->ImportAsset(path);
                if (handle.IsValid())
                {
                    m_RefreshRequested = true;
                    m_NotificationManager.AddNotification("Imported asset: " + path.string(), Notification::Type::Info);
                }
                else
                {
                    m_NotificationManager.AddNotification("Failed to import asset: " + path.string(),
                                                          Notification::Type::Error);
                }

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
                m_RefreshRequested = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Reveal in Explorer"))
            {
                if (!FileOperations::RevealInFileExplorer(path.string().c_str()))
                {
                    m_NotificationManager.AddNotification("Could not reveal folder in explorer: " + path.string(),
                                                          Notification::Type::Error);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Create AI State Tree...") && m_OnCreateAIStateTree)
            {
                m_PendingCreateAITreeFolder = path;
                std::snprintf(m_NewAITreeFileName, sizeof(m_NewAITreeFileName),
                              "NewAIStateTree.kbrai");
                m_OpenCreateAITreePopup = true;
                ImGui::CloseCurrentPopup();
            }

            constexpr const char* deleteFolderPopup = "DeleteFolderPopup";
            if (ImGui::MenuItem("Delete Folder"))
            {
                ImGui::OpenPopup(deleteFolderPopup);

                ImGui::CloseCurrentPopup();
            }
            if (ImGui::BeginPopupModal(deleteFolderPopup))
            { 
                // TODO: Not working as expected, the popup doesn't show up
                if (ImGui::Button("Confirm Delete"))
                {
                    std::filesystem::remove_all(path);
                    m_RefreshRequested = true;
                }

                ImGui::EndPopup();
            }
            ImGui::EndPopup();
        }
    }

	void AssetsPanel::ShowContextMenu(const ImGuiPopupFlags popupFlags)
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
                m_RefreshRequested = true;
            }

            ImGui::Text("Create basic asset");
            ImGui::Dummy(ImVec2(20, 0));
            ImGui::SameLine();

            // Creating basic assets
            if (ImGui::MenuItem("Material"))
            {
                const std::string materialPathStr =
                    FileDialog::SaveFile("Kerberos Material (*.kbrmat)\0*.kbrmat\0", "kbrmat");
                const std::filesystem::path materialPath = materialPathStr;
                if (!materialPathStr.empty())
                {
                    Material material;
                    material.Name = materialPath.stem().string();
                    material.Params.AlbedoFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    material.Params.MetallicFactor = 0.0f;
                    material.Params.RoughnessFactor = 1.0f;

                    const std::filesystem::path assetPath =
                        MakeLexicallyRelative(materialPath, Project::GetProjectDirectory());
                    if (!MaterialImporter::SaveMaterial(assetPath, material))
                    {
                        Log::EditorError("Could not create material file at path: {0}", materialPathStr);
                    }
                    else
                    {
                        [[maybe_unused]]
                        const AssetHandle handle =
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
        KBR_TRACY_FUNCTION();

        m_ContentItems.clear();
        m_CurrentDirectoryRelativePath = m_CurrentDirectory.lexically_relative(m_AssetsDirectory);
        m_SubAssetsByRoot.clear();
        m_CachedArtifactExists.clear();
        m_MetadataByHandle.clear();

        AssetPicker::InvalidateCache();

        const auto editorAssetManager = Project::GetActive()->GetEditorAssetManager();
        const auto& registry = editorAssetManager->GetAssetRegistry();

        // Build one normalized Filepath -> handle index for this refresh. The registry's
        // ContainsPath/GetHandle helpers are intentionally linear and must not be called
        // once per filesystem entry.
        std::unordered_map<std::string, AssetHandle> handleByRelativePath;
        handleByRelativePath.reserve(registry.Count());
        for (const auto& [handle, metadata] : registry)
        {
            m_MetadataByHandle.emplace(handle, metadata);
            if (!metadata.Filepath.empty())
                handleByRelativePath.try_emplace(NormalizeAssetPath(metadata.Filepath), handle);

            bool artifactExists = false;
            if (!metadata.LibraryPath.empty())
            {
                const auto artifactPath = metadata.LibraryPath.is_absolute()
                                              ? metadata.LibraryPath
                                              : (m_AssetsDirectory / "Cache" / metadata.LibraryPath);
                std::error_code ec;
                artifactExists = std::filesystem::is_regular_file(artifactPath, ec) && !ec;
            }
            m_CachedArtifactExists[handle] = artifactExists;

            AssetHandle rootHandle = AssetHandle::Invalid();
            if (metadata.RootSourceHandle.IsValid() && metadata.RootSourceHandle != handle)
                rootHandle = metadata.RootSourceHandle;
            else if (metadata.ParentHandle.IsValid() && metadata.ParentHandle != handle)
                rootHandle = metadata.ParentHandle;
            if (rootHandle.IsValid())
                m_SubAssetsByRoot[rootHandle].push_back(handle);
        }

        for (auto& children : m_SubAssetsByRoot | std::views::values)
        {
            std::ranges::sort(children, [this](const AssetHandle left, const AssetHandle right) {
                const auto leftIt = m_MetadataByHandle.find(left);
                const auto rightIt = m_MetadataByHandle.find(right);
                if (leftIt == m_MetadataByHandle.end())
                    return false;
                if (rightIt == m_MetadataByHandle.end())
                    return true;
                const auto& leftMetadata = leftIt->second;
                const auto& rightMetadata = rightIt->second;
                if (leftMetadata.Type != rightMetadata.Type)
                    return leftMetadata.Type < rightMetadata.Type;
                if (leftMetadata.SubAssetKey != rightMetadata.SubAssetKey)
                    return leftMetadata.SubAssetKey < rightMetadata.SubAssetKey;
                return leftMetadata.LibraryPath.generic_string() < rightMetadata.LibraryPath.generic_string();
            });
        }

        for (std::error_code error;
             const auto& entry : std::filesystem::directory_iterator(
                 m_CurrentDirectory, std::filesystem::directory_options::skip_permission_denied, error))
        {
            if (error)
            {
                Log::EditorError("Failed to get file entry: {}", error.message());
                error.clear();
                continue;
            }

            const auto absolutePath = MakeAbsoluteLexical(entry.path());
            const auto relativePath = absolutePath.lexically_relative(m_AssetsDirectory);
            if (relativePath.empty() || relativePath.is_absolute() ||
                (!relativePath.empty() && *relativePath.begin() == ".."))
                continue;

            bool hidden = false;
            for (const auto& component : relativePath)
                hidden |= component.string().starts_with('.');

            const bool isDirectory = entry.is_directory(error);
            if (error)
            {
                error.clear();
                continue;
            }
            const bool isMetaFile = entry.path().extension() == ".meta";
            const bool isAssetRegistry = entry.path().extension() == ".kbrar";
            const bool isCacheDir = entry.path().filename() == "Cache" || entry.path().filename() == "Staging";
            if (hidden || (isDirectory && isCacheDir) || isMetaFile || isAssetRegistry)
                continue;

            AssetHandle handle = AssetHandle::Invalid();
            if (!isDirectory && entry.is_regular_file(error) && !error)
            {
                const auto it = handleByRelativePath.find(NormalizeAssetPath(relativePath));
                if (it != handleByRelativePath.end())
                    handle = it->second;
            }
            error.clear();

            const std::string relativePathString = relativePath.generic_string();
            m_ContentItems.push_back({ .Path = absolutePath,
                                       .Handle = handle,
                                       .IsDirectory = isDirectory,
                                       .RelativePath = relativePath,
                                       .Name = absolutePath.filename().string(),
                                       .RelativePathString = relativePathString });
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

        const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
        if (!assetManager)
        {
            m_NotificationManager.AddNotification("Could not open material: no editor asset manager",
                                                  Notification::Type::Error);
            return;
        }

        try
        {
            const AssetHandle handle = assetManager->ImportAsset(absolutePath);
            if (!handle.IsValid())
            {
                m_NotificationManager.AddNotification("Could not import material: " + absolutePath.string(),
                                                      Notification::Type::Error);
                return;
            }

            const Ref<Material> material = AssetManager::GetAsset<Material>(handle);
            if (!material)
            {
                m_NotificationManager.AddNotification("Could not load material: " + absolutePath.string(),
                                                      Notification::Type::Error);
                return;
            }

            MaterialEditorState state;
            state.Filepath = absolutePath;
            state.Handle = handle;
            state.WorkingCopy = material;
            state.Open = true;

            m_OpenMaterialEditors.emplace(key, std::move(state));
        }
        catch (const std::exception& exception)
        {
            m_NotificationManager.AddNotification(
                "Could not open material: " + absolutePath.string() + "\n" + exception.what(),
                Notification::Type::Error);
        }
    }

	void AssetsPanel::DrawMaterialTextureField(const char* label, Ref<Texture2D>& texture)
    {
        const std::string_view name(label);
        EditorIcon icon = EditorIcon::SlotUnset;
        if (name.find("Albedo") != std::string_view::npos)
            icon = EditorIcon::SlotAlbedo;
        else if (name.find("Normal") != std::string_view::npos)
            icon = EditorIcon::SlotNormal;
        else if (name.find("Metallic") != std::string_view::npos)
            icon = EditorIcon::SlotMetallic;
        else if (name.find("Roughness") != std::string_view::npos)
            icon = EditorIcon::SlotRoughness;
        else if (name.find("AO") != std::string_view::npos)
            icon = EditorIcon::SlotOcclusion;
        else if (name.find("Emissive") != std::string_view::npos)
            icon = EditorIcon::SlotEmissive;

        EditorIcons::Draw(icon);
        ImGui::SameLine(0.0f, 4.0f);
        AssetPicker::DrawAsset(label, AssetType::Texture2D, texture, "Search textures...");

        const bool dataSlot = name.find("Normal") != std::string_view::npos ||
                              name.find("Metallic") != std::string_view::npos ||
                              name.find("Roughness") != std::string_view::npos ||
                              name.find("AO") != std::string_view::npos;
        const bool colorSlot = name.find("Albedo") != std::string_view::npos ||
                               name.find("Emissive") != std::string_view::npos;

        if (texture && dataSlot && texture->IsSrgb())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "sRGB data");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Normal, metallic, roughness, and AO maps are data, not color.\n"
                                  "Turn off sRGB in the texture inspector so the shader reads linear values.");
            }
        }
        else if (texture && colorSlot && !texture->IsSrgb())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "linear color");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Albedo and emissive maps are authored in sRGB.\n"
                                  "Turn on sRGB in the texture inspector so sampling decodes them to linear.");
            }
        }
    }

	std::filesystem::path AssetsPanel::GetRelativePath(const std::filesystem::path& path) const
    {
        KBR_TRACY_FUNCTION();

        const auto normalizedPath = path.is_absolute() ? path.lexically_normal() : MakeAbsoluteLexical(path);
        return normalizedPath.lexically_relative(m_AssetsDirectory);
    }

	void AssetsPanel::RenderMaterialEditors()
    {
        KBR_TRACY_FUNCTION();

        for (auto it = m_OpenMaterialEditors.begin(); it != m_OpenMaterialEditors.end();)
        {
            auto& [key, state] = *it;
            bool open = state.Open;
            const std::string windowTitle =
                std::string("Material Editor - ") + state.Filepath.filename().string() + "##" + key;

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

                const bool linearMaterial =
                    state.WorkingCopy->ColorEncoding == ColorEncoding::Linear;
                ImGui::TextDisabled("Color encoding: %s", linearMaterial
                    ? "Linear (glTF 2.0 factors)"
                    : "sRGB (authored)");

                const auto editColor4 = [&](const char* label, glm::vec4& value) {
                    if (!linearMaterial)
                        return Editor::ColorWidgets::SrgbColorEdit4(label, value);

                    glm::vec4 display = LinearToSrgb(value);
                    if (!Editor::ColorWidgets::SrgbColorEdit4(label, display))
                        return false;
                    value = SrgbToLinear(display);
                    return true;
                };
                const auto editColor3 = [&](const char* label, glm::vec3& value) {
                    if (!linearMaterial)
                        return Editor::ColorWidgets::SrgbColorEdit3(label, value);

                    glm::vec3 display = LinearToSrgb(value);
                    if (!Editor::ColorWidgets::SrgbColorEdit3(label, display))
                        return false;
                    value = SrgbToLinear(display);
                    return true;
                };

                editColor4("Albedo", state.WorkingCopy->Params.AlbedoFactor);
                ImGui::DragFloat("Roughness", &state.WorkingCopy->Params.RoughnessFactor, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Metallic", &state.WorkingCopy->Params.MetallicFactor, 0.01f, 0.0f, 1.0f);
                editColor3("Emissive Color", state.WorkingCopy->EmissiveColor);
                ImGui::DragFloat("Emissive Intensity", &state.WorkingCopy->EmissiveIntensity, 1.0f, 0.0f, 1000.0f);
                state.WorkingCopy->Params.Emissive =
                    state.WorkingCopy->EmissiveColor * state.WorkingCopy->EmissiveIntensity;
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
                    const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
                    if (!assetManager)
                    {
                        m_NotificationManager.AddNotification("Failed to save material: no editor asset manager",
                                                              Notification::Type::Error);
                    }
                    else if (!MaterialImporter::SaveMaterial(state.Filepath, *state.WorkingCopy))
                    {
                        m_NotificationManager.AddNotification("Failed to save material: " + state.Filepath.string(),
                                                              Notification::Type::Error);
                    }
                    else
                    {
                        const AssetHandle materialHandle = state.Handle.IsValid()
                            ? state.Handle
                            : assetManager->ImportAsset(state.Filepath);

                        // Update the already-loaded instance before ReimportAsset evicts it,
                        // so scene objects holding this material see edits immediately.
                        if (materialHandle.IsValid())
                        {
                            try
                            {
                                if (const Ref<Material> loadedMaterial = AssetManager::GetAsset<Material>(materialHandle))
                                {
                                    *loadedMaterial = *state.WorkingCopy;
                                    loadedMaterial->GetHandle() = materialHandle;
                                }
                            }
                            catch (const std::exception& exception)
                            {
                                Log::EditorWarn("Could not update loaded material {}: {}", materialHandle,
                                                exception.what());
                            }
                        }

                        const AssetHandle rebuiltHandle = assetManager->ReimportAsset(state.Filepath);
                        if (!rebuiltHandle.IsValid())
                        {
                            m_NotificationManager.AddNotification(
                                "Material saved, but reimport failed: " + state.Filepath.string(),
                                Notification::Type::Error);
                        }
                        else
                        {
                            state.Handle = rebuiltHandle;
                            m_NotificationManager.AddNotification("Saved material: " + state.Filepath.string(),
                                                                  Notification::Type::Info);
                        }
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
        KBR_TRACY_FUNCTION();

        if (!ImGui::Button("Import Asset"))
            return;

        const std::string filePath = FileDialog::OpenFile("All Files (*.*)\0*.*\0");
        if (filePath.empty())
            return;

        const auto assetPath = MakeAbsoluteLexical(filePath);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(assetPath, ec) || ec)
        {
            m_NotificationManager.AddNotification("File does not exist: " + assetPath.string(),
                                                  Notification::Type::Error);
            return;
        }

        const auto relativePath = assetPath.lexically_relative(m_AssetsDirectory);
        const bool insideAssets = !relativePath.empty() && !relativePath.is_absolute() && *relativePath.begin() != "..";
        if (!insideAssets)
        {
            m_NotificationManager.AddNotification(
                "Asset must be located inside the Assets directory: " + assetPath.string(), Notification::Type::Error);
            return;
        }

        const AssetHandle handle = Project::GetActive()->GetEditorAssetManager()->ImportAsset(assetPath);
        if (handle.IsValid())
        {
            RefreshAssetTree();
            m_NotificationManager.AddNotification("Imported asset: " + assetPath.string(), Notification::Type::Info);
        }
        else
        {
            m_NotificationManager.AddNotification("Failed to import asset: " + assetPath.string(),
                                                  Notification::Type::Error);
        }
    }

	void AssetsPanel::HandleAssetDragAndDrop(const AssetHandle handle,
                                             const std::filesystem::path& filename,
                                             const AssetMetadata& metadata)
    {
        KBR_TRACY_FUNCTION();

        // Dragging an entry whose imported artifact is missing (or not yet produced)
        // only creates a payload that consumers cannot load. Check the refresh-time
        // snapshot instead of calling IsAssetHandleValid(), which hits the filesystem.
        const auto artifactIt = m_CachedArtifactExists.find(handle);
        if (artifactIt == m_CachedArtifactExists.end() || !artifactIt->second)
            return;

        if (!ImGui::BeginDragDropSource())
            return;

        const AssetDragPayload dragPayload{
            .Handle = handle, .RootHandle = metadata.RootSourceHandle.IsValid() ? metadata.RootSourceHandle : handle
        };

        const char* payloadType = nullptr;
        switch (metadata.Type)
        {
        case AssetType::Texture2D:
            payloadType = assetBrowserTexture;
            break;
        case AssetType::TextureCube:
            payloadType = assetBrowserTextureCube;
            break;
        case AssetType::Mesh:
            payloadType = assetBrowserMesh;
            break;
        case AssetType::Prefab:
            payloadType = assetBrowserPrefab;
            break;
        case AssetType::Sound:
            payloadType = assetBrowserAudio;
            break;
        case AssetType::Material:
            payloadType = assetBrowserMaterial;
            break;
        case AssetType::Skeleton:
            payloadType = assetBrowserSkeleton;
            break;
        case AssetType::SkinnedMesh:
            payloadType = assetBrowserSkinnedMesh;
            break;
        case AssetType::Animation:
            payloadType = assetBrowserAnimation;
            break;
        case AssetType::AIStateTree:
            payloadType = assetBrowserAIStateTree;
            break;
        default:
            payloadType = assetBrowserItem;
            break;
        }

        if (payloadType)
            ImGui::SetDragDropPayload(payloadType, &dragPayload, sizeof(dragPayload), ImGuiCond_Once);

        ImGui::TextUnformatted(filename.filename().string().c_str());
        ImGui::Text("Type: %.*s",
                    static_cast<int>(AssetTypeToString(metadata.Type).size()),
                    AssetTypeToString(metadata.Type).data());

        ImGui::EndDragDropSource();
    }

	void AssetsPanel::SetCurrentDir(const std::filesystem::path& path)
	{
		KBR_TRACY_FUNCTION();

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
