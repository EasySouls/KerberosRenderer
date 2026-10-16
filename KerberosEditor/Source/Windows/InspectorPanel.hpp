#pragma once

#include "EditorWindow.hpp"
#include "Assets/Asset.hpp"
#include "Core/Core.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace Kerberos
{
	class HierarchyPanel;
	class AssetsPanel;
	class Texture2D;
    struct AssetSelection;

	/**
	* The unified right-side inspector window. It shows entity components when an
	* entity is selected and asset import options when a source asset is selected
	* in the asset browser.
	*/
	class InspectorPanel : public EditorWindow
	{
	public:
		InspectorPanel() = default;
		~InspectorPanel() override = default;

		void SetContext(HierarchyPanel* hierarchy, AssetsPanel* assets);

		void OnEvent(Event& event) override;
		void OnImGuiRender() override;

	private:
		void DrawAssetOptions(const AssetSelection& selection);
		void DrawTexturePreview(const std::filesystem::path& sourcePath, bool srgb);

		HierarchyPanel* m_Hierarchy = nullptr;
		AssetsPanel* m_Assets = nullptr;

		// Draft import options for the currently inspected source (Apply/Revert).
		std::filesystem::path m_DraftSourcePath;
		std::map<std::string, bool> m_DraftSrgb;
		bool m_DraftDirty = false;

		// Cached preview for a standalone raster texture.
		Ref<Texture2D> m_PreviewTexture;
		std::filesystem::path m_PreviewPath;
		bool m_PreviewSrgb = false;
	};
}
