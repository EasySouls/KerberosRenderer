#include "InspectorPanel.hpp"

#include "Windows/AssetsPanel.hpp"
#include "Windows/HierarchyPanel.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "Project/Project.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "VulkanContext.hpp"
#include "Profiling/Profilers.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <ranges>
#include <vector>

namespace Kerberos
{
	void InspectorPanel::SetContext(HierarchyPanel* hierarchy, AssetsPanel* assets)
	{
		m_Hierarchy = hierarchy;
		m_Assets = assets;
	}

	void InspectorPanel::OnEvent(Event& /*event*/)
	{
	}

	void InspectorPanel::OnImGuiRender()
	{
		KBR_TRACY_FUNCTION();

		ImGui::Begin("Properties");

		const bool hasAsset = m_Assets && m_Assets->HasAssetSelection();
		const Entity selectedEntity = m_Hierarchy ? m_Hierarchy->GetSelectedEntity() : Entity{};

		if (hasAsset)
		{
			DrawAssetOptions(m_Assets->GetSelectedAsset());
		}
		else if (selectedEntity && m_Hierarchy)
		{
			m_Hierarchy->DrawComponents(selectedEntity);
			m_Hierarchy->AddComponentPopup(selectedEntity);
		}
		else
		{
			ImGui::TextDisabled("No selection");
		}

		ImGui::End();
	}

	void InspectorPanel::DrawAssetOptions(const AssetSelection& selection)
	{
        const auto& [handle, sourcePath, type] = selection;

		// (Re)load the persisted options into the draft when the inspected source changes.
		if (sourcePath != m_DraftSourcePath)
		{
			m_DraftSourcePath = sourcePath;
			m_DraftSrgb.clear();
			m_DraftDirty = false;
			m_PreviewTexture = nullptr;

			const auto manager = Project::GetActive()->GetEditorAssetManager();
			if (const auto meta = manager->GetMetaFile(sourcePath); meta.has_value())
				m_DraftSrgb = meta->TextureSrgb;
		}

		const std::string assetTypeStr = std::string(AssetTypeToString(type));
		ImGui::TextUnformatted(sourcePath.filename().string().c_str());
		ImGui::TextDisabled("Type: %s", assetTypeStr.data());
		if (handle.IsValid())
			ImGui::TextDisabled("Handle: %llu", static_cast<unsigned long long>(static_cast<uint64_t>(handle)));
		ImGui::Separator();

		const bool isTexture = type == AssetType::Texture2D || type == AssetType::TextureCube;
		const bool isPrefab = type == AssetType::Prefab;

		if (isTexture)
		{
			if (!m_DraftSrgb.contains("source"))
				m_DraftSrgb["source"] = true;
			bool& srgb = m_DraftSrgb["source"];
			if (ImGui::Checkbox("sRGB (color texture)", &srgb))
				m_DraftDirty = true;

			ImGui::Separator();
			DrawTexturePreview(sourcePath, srgb);
		}
		else if (isPrefab)
		{
			const auto manager = Project::GetActive()->GetEditorAssetManager();
			const auto& registry = manager->GetAssetRegistry();

			std::vector<AssetHandle> textures;
			for (const auto& [texHandle, md] : registry)
				if (md.RootSourceHandle == handle && md.Type == AssetType::Texture2D)
					textures.push_back(texHandle);
			std::ranges::sort(textures, [&](const AssetHandle left, const AssetHandle right) {
				return registry.Get(left).SubAssetKey < registry.Get(right).SubAssetKey;
			});

			if (textures.empty())
			{
				ImGui::TextDisabled("No textures in this model.");
			}
			else
			{
				ImGui::Text("Textures");
				for (const AssetHandle texHandle : textures)
				{
					const std::string key = registry.Get(texHandle).SubAssetKey;
					bool value = m_DraftSrgb.contains(key) ? m_DraftSrgb[key] : true;
					ImGui::PushID(key.c_str());
					if (ImGui::Checkbox("sRGB", &value))
					{
						m_DraftSrgb[key] = value;
						m_DraftDirty = true;
					}
					ImGui::PopID();
					ImGui::SameLine();
					ImGui::TextUnformatted(key.c_str());
				}
			}
		}
		else
		{
			ImGui::TextDisabled("No import options for this asset type.");
		}

		if (isTexture || isPrefab)
		{
			ImGui::Separator();
			ImGui::BeginDisabled(!m_DraftDirty);
			if (ImGui::Button("Apply"))
			{
				const auto manager = Project::GetActive()->GetEditorAssetManager();
				const AssetHandle rootHandle = manager->SetTextureSrgbOptions(sourcePath, m_DraftSrgb);
				if (rootHandle.IsValid())
				{
					m_DraftDirty = false;
					m_PreviewTexture = nullptr;
					if (m_Assets)
						m_Assets->RequestRefresh();
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::BeginDisabled(!m_DraftDirty);
			if (ImGui::Button("Revert"))
			{
				// Force a reload of the persisted options next frame.
				m_DraftSourcePath.clear();
				m_PreviewTexture = nullptr;
			}
			ImGui::EndDisabled();
		}
	}

    static constexpr bool IsRasterImage(const std::filesystem::path& path)
    {
        const std::string extension = path.extension().string();
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg"
            || extension == ".bmp" || extension == ".tga" || extension == ".gif" || extension == ".psd"
            || extension == ".hdr" || extension == ".pic" || extension == ".pnm";
    }

	void InspectorPanel::DrawTexturePreview(const std::filesystem::path& sourcePath, const bool srgb)
	{
		// Only raster source images get an in-process preview here.
        const bool isRaster = IsRasterImage(sourcePath);
		if (!isRaster)
			return;

		if (!m_PreviewTexture || m_PreviewPath != sourcePath || m_PreviewSrgb != srgb)
		{
			auto [spec, buffer] = TextureImporter::LoadTextureData(sourcePath, /*flip=*/true, /*desiredChannels=*/0, srgb);
			if (buffer.Data && buffer.Size > 0)
			{
				m_PreviewTexture = Texture2D::FromBuffer(spec, buffer);
				m_PreviewPath = sourcePath;
				m_PreviewSrgb = srgb;
			}
		}

		if (!m_PreviewTexture)
			return;

        constexpr float maxSize = 200.0f;
		const float width = static_cast<float>(std::max(1u, m_PreviewTexture->width));
		const float height = static_cast<float>(std::max(1u, m_PreviewTexture->height));
		float displayWidth = maxSize;
		float displayHeight = maxSize;
		if (width >= height)
			displayHeight = maxSize * (height / width);
		else
			displayWidth = maxSize * (width / height);

		const uint64_t textureID = VulkanContext::Get().GetImGuiRendererID(m_PreviewTexture);
		ImGui::Image(textureID, ImVec2{ displayWidth, displayHeight }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
	}
}
