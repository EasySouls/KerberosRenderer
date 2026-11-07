#pragma once

#include "EditorWindow.hpp"
#include "Assets/Pipeline/AssetMeta.hpp"
#include "Core/UUID.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace Kerberos
{
	class Scene;

	/// Timeline editor for authored animation markers and typed events. 
	/// Edits the source `.meta` through EditorAssetManager and reimports the clip on save.
	class AnimationEditorPanel final : public EditorWindow
	{
	public:
		AnimationEditorPanel() = default;
		~AnimationEditorPanel() override = default;

		void OnImGuiRender() override;
		void OnEvent(Event& event) override { (void)event; }

		void SetContext(const Ref<Scene>& scene, UUID selectedEntity, bool canEdit);
		void SetOpen(const bool open) { m_Open = open; }
		bool IsOpen() const { return m_Open; }

	private:
		struct ClipSource
		{
			bool Valid = false;
			std::filesystem::path SourcePath;
			std::string ClipKey;
			AssetHandle ClipHandle = AssetHandle::Invalid();
			float Duration = 0.0f;
		};

		void ResolveAnimator();
		void RefreshClipSource();
		void LoadMeta();
		bool SaveMeta();
		void RevertMeta();

		void DrawToolbar();
		void DrawTimeline(float duration);
		void DrawEventInspector();
		void DrawMarkerInspector();
		void DrawParameterEditor(AnimationEventParameterFile& parameter, int32_t parameterIndex);
		void DrawValidation();

		void EnsureClipEntry();
		AnimationClipMetaEntry* GetEditingClip();
		void AddEvent(bool duration);
		void AddMarker();
		void DeleteSelectedEvent();
		void DeleteSelectedMarker();

        static float TimeToX(float time, float originX, float pixelsPerSecond);
        static float XToTime(float x, float originX, float pixelsPerSecond);

	private:
		bool m_Open = false;
		Ref<Scene> m_Scene;
		UUID m_SelectedEntityUUID = UUID::Invalid();
		bool m_CanEdit = true;

		UUID m_AnimatorEntityUUID = UUID::Invalid();
		int32_t m_SelectedClipIndex = -1;
		AssetHandle m_LoadedClipHandle = AssetHandle::Invalid();
		ClipSource m_ClipSource;
		std::string m_SourceError;

		std::map<std::string, AnimationClipMetaEntry> m_ClipMeta;
		std::filesystem::path m_LoadedSourcePath;
		bool m_MetaLoaded = false;
		bool m_Dirty = false;

		int32_t m_SelectedEventIndex = -1;
		int32_t m_SelectedMarkerIndex = -1;

		float m_PixelsPerSecond = 240.0f;
		bool m_Scrubbing = false;
	};
}
