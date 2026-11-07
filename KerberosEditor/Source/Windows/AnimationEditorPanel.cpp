#include "AnimationEditorPanel.hpp"

#include "Assets/AssetManager.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Project/Project.hpp"
#include "Renderer/Animation/AnimationClip.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Widgets/AssetPicker.hpp"

#include <imgui/imgui.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <map>
#include <vector>

import Kerberos;

namespace Kerberos {

namespace {

const char* ParameterTypeLabel(const AnimationEventParameterTypeFile type)
{
	switch (type)
	{
	case AnimationEventParameterTypeFile::Bool: return "Bool";
	case AnimationEventParameterTypeFile::Int: return "Int";
	case AnimationEventParameterTypeFile::Float: return "Float";
	case AnimationEventParameterTypeFile::Vector3: return "Vector3";
	case AnimationEventParameterTypeFile::String: return "String";
	case AnimationEventParameterTypeFile::Asset: return "Asset";
	}
	return "Float";
}

bool InputString(const char* label, std::string& value)
{
	char buffer[512];
	const size_t length = std::min(value.size(), sizeof(buffer) - 1);
	if (length > 0)
		std::memcpy(buffer, value.data(), length);
	buffer[length] = '\0';

	if (ImGui::InputText(label, buffer, sizeof(buffer)))
	{
		value = buffer;
		return true;
	}

	return false;
}

bool InputMultiline(const char* label, std::string& value)
{
	std::vector<char> buffer(std::max<size_t>(value.size() + 1, 4096), '\0');
	std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());

	if (ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), ImVec2(-1.0f, 80.0f)))
	{
		value = buffer.data();
		return true;
	}

	return false;
}

std::string GetClipLabel(const AnimatorComponent& animator, const int32_t index)
{
	if (index < 0 || index >= static_cast<int32_t>(animator.AnimationClips.size()))
		return "None";

	const size_t clipIndex = static_cast<size_t>(index);
	if (clipIndex < animator.LoadedClips.size() && animator.LoadedClips[clipIndex])
		return animator.LoadedClips[clipIndex]->GetName();

	if (animator.AnimationClips[clipIndex].IsValid())
	{
		if (const Ref<AnimationClip> clip = AssetManager::AsAsset<AnimationClip>(
				animator.AnimationClips[clipIndex]))
		{
			return clip->GetName();
		}
	}

	return "Clip " + std::to_string(index);
}

}

void AnimationEditorPanel::SetContext(const Ref<Scene>& scene, const UUID selectedEntity, const bool canEdit)
{
	m_Scene = scene;
	m_SelectedEntityUUID = selectedEntity;
	m_CanEdit = canEdit;
}

void AnimationEditorPanel::OnImGuiRender()
{
	if (!m_Open)
		return;

	ResolveAnimator();

	const bool contextChanged = m_AnimatorEntityUUID != UUID::Invalid() &&
		m_SelectedClipIndex >= 0;
	(void)contextChanged;

	RefreshClipSource();

	ImGui::SetNextWindowSize(ImVec2(1000.0f, 620.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Animation Editor", &m_Open))
	{
		ImGui::End();
		return;
	}

	DrawToolbar();

	if (m_ClipSource.Valid)
	{
		DrawTimeline(m_ClipSource.Duration);

		ImGui::BeginChild("##AnimationEditorInspector", ImVec2(0.0f, 0.0f), true);
		DrawEventInspector();
		ImGui::Separator();
		DrawMarkerInspector();
		ImGui::Separator();
		DrawValidation();
		ImGui::EndChild();
	}
	else
	{
		ImGui::TextDisabled("Select an animation clip with editable source metadata.");
		if (!m_SourceError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "%s", m_SourceError.c_str());
	}

	ImGui::End();
}

void AnimationEditorPanel::ResolveAnimator()
{
	m_AnimatorEntityUUID = UUID::Invalid();

	if (!m_Scene || !m_SelectedEntityUUID.IsValid())
		return;

	Entity entity = m_Scene->TryGetEntityByUUID(m_SelectedEntityUUID);
	if (!entity)
		return;

	for (Entity current = entity; current; current = m_Scene->GetParent(current))
	{
		if (current.HasComponent<AnimatorComponent>())
		{
			m_AnimatorEntityUUID = current.GetUUID();
			return;
		}
	}

	if (entity.HasComponent<SkeletonComponent>())
	{
		if (const Entity animator = m_Scene->FindAnimatorEntityForSkeleton(entity.GetUUID()))
			m_AnimatorEntityUUID = animator.GetUUID();
	}
}

void AnimationEditorPanel::RefreshClipSource()
{
	m_SourceError.clear();

	if (!m_Scene || !m_AnimatorEntityUUID.IsValid())
	{
		m_SelectedClipIndex = -1;
		m_ClipSource = {};
		m_MetaLoaded = false;
		m_LoadedClipHandle = AssetHandle::Invalid();
		return;
	}

	const Entity animatorEntity = m_Scene->TryGetEntityByUUID(m_AnimatorEntityUUID);
	if (!animatorEntity || !animatorEntity.HasComponent<AnimatorComponent>())
	{
		m_SelectedClipIndex = -1;
		m_ClipSource = {};
		m_MetaLoaded = false;
		m_LoadedClipHandle = AssetHandle::Invalid();
		return;
	}

	const auto& animator = animatorEntity.GetComponent<AnimatorComponent>();
	if (animator.AnimationClips.empty())
	{
		m_SelectedClipIndex = -1;
		m_ClipSource = {};
		m_MetaLoaded = false;
		m_LoadedClipHandle = AssetHandle::Invalid();
		return;
	}

	const int32_t clipCount = static_cast<int32_t>(animator.AnimationClips.size());
	const bool followActiveClip = m_CanEdit && animator.ActiveClip >= 0 &&
		animator.ActiveClip < clipCount && animator.ActiveClip != m_SelectedClipIndex;

	if (m_SelectedClipIndex < 0 || m_SelectedClipIndex >= clipCount || followActiveClip)
	{
		m_SelectedClipIndex = animator.ActiveClip >= 0 && animator.ActiveClip < clipCount
			? animator.ActiveClip
			: 0;
	}

	const AssetHandle handle = animator.AnimationClips[static_cast<size_t>(m_SelectedClipIndex)];
	if (handle == m_LoadedClipHandle && m_MetaLoaded)
	{
		if (m_ClipSource.Valid &&
			static_cast<size_t>(m_SelectedClipIndex) < animator.LoadedClips.size() &&
			animator.LoadedClips[static_cast<size_t>(m_SelectedClipIndex)])
		{
			m_ClipSource.Duration =
				animator.LoadedClips[static_cast<size_t>(m_SelectedClipIndex)]->GetDuration();
		}
		return;
	}

	m_LoadedClipHandle = handle;
	m_ClipSource = {};
	m_MetaLoaded = false;
	m_SelectedEventIndex = -1;
	m_SelectedMarkerIndex = -1;

	const Ref<Project> project = Project::GetActive();
	const Ref<EditorAssetManager> manager = project ? project->GetEditorAssetManager() : nullptr;
	if (!manager)
	{
		m_SourceError = "No editor asset manager is active.";
		return;
	}

	if (!handle.IsValid() || !manager->IsAssetHandleValid(handle))
	{
		m_SourceError = "The selected clip has no editable source asset.";
		return;
	}

	const AssetMetadata& clipMetadata = manager->GetMetadata(handle);
	if (clipMetadata.Type != AssetType::Animation)
	{
		m_SourceError = "The selected asset is not an animation clip.";
		return;
	}

	const AssetHandle rootHandle = clipMetadata.RootSourceHandle.IsValid()
		? clipMetadata.RootSourceHandle
		: handle;
	const AssetMetadata& rootMetadata = manager->GetMetadata(rootHandle);

	if (rootMetadata.SourceRoot == AssetSourceRoot::Engine)
	{
		m_SourceError = "Engine assets are read-only.";
		return;
	}

	if (rootMetadata.Filepath.empty())
	{
		m_SourceError = "The clip source path is missing.";
		return;
	}

	std::filesystem::path sourcePath = rootMetadata.Filepath;
	if (sourcePath.is_relative())
		sourcePath = Project::GetAssetDirectory() / sourcePath;

	const auto metaFile = manager->GetMetaFile(sourcePath);
	if (!metaFile || metaFile->ImporterType != ImporterType::GLTFScene)
	{
		m_SourceError = "This clip's source importer does not support authored events.";
		return;
	}

	if (clipMetadata.SubAssetKey.empty())
	{
		m_SourceError = "The clip has no stable sub-asset key.";
		return;
	}

	m_ClipSource.Valid = true;
	m_ClipSource.SourcePath = sourcePath;
	m_ClipSource.ClipKey = clipMetadata.SubAssetKey;
	m_ClipSource.ClipHandle = handle;

	if (static_cast<size_t>(m_SelectedClipIndex) < animator.LoadedClips.size() &&
		animator.LoadedClips[static_cast<size_t>(m_SelectedClipIndex)])
	{
		m_ClipSource.Duration =
			animator.LoadedClips[static_cast<size_t>(m_SelectedClipIndex)]->GetDuration();
	}
	else if (const Ref<AnimationClip> clip = AssetManager::AsAsset<AnimationClip>(handle))
	{
		m_ClipSource.Duration = clip->GetDuration();
	}

	// Clips from the same source share one working metadata map. Keep unsaved edits when the
	// user switches between clips of the same glTF; only reload when the source changes.
	if (m_MetaLoaded && m_LoadedSourcePath == m_ClipSource.SourcePath)
	{
		m_SelectedEventIndex = -1;
		m_SelectedMarkerIndex = -1;
		EnsureClipEntry();
	}
	else
	{
		if (m_Dirty)
		{
			Log::CoreWarn("Discarding unsaved animation event edits because the selected "
			              "clip source changed");
		}
		LoadMeta();
	}
}

void AnimationEditorPanel::LoadMeta()
{
	m_ClipMeta.clear();
	m_MetaLoaded = false;
	m_Dirty = false;
	m_SelectedEventIndex = -1;
	m_SelectedMarkerIndex = -1;

	if (!m_ClipSource.Valid)
		return;

	const Ref<Project> project = Project::GetActive();
	const Ref<EditorAssetManager> manager = project ? project->GetEditorAssetManager() : nullptr;
	if (!manager)
		return;

	if (const auto meta = manager->GetAnimationClipMeta(m_ClipSource.SourcePath))
		m_ClipMeta = *meta;

	m_LoadedSourcePath = m_ClipSource.SourcePath;
	m_MetaLoaded = true;
	EnsureClipEntry();
}

bool AnimationEditorPanel::SaveMeta()
{
	if (!m_ClipSource.Valid || !m_Dirty)
		return false;

	const Ref<Project> project = Project::GetActive();
	const Ref<EditorAssetManager> manager = project ? project->GetEditorAssetManager() : nullptr;
	if (!manager)
		return false;

	const AssetHandle result = manager->SetAnimationClipMeta(m_ClipSource.SourcePath, m_ClipMeta);
	if (!result.IsValid())
	{
		Log::CoreError("Failed to save animation events for {}", m_ClipSource.SourcePath.string());
		return false;
	}

	m_Dirty = false;

	// Force the runtime to resolve the reimported clip on the next animation update.
	if (m_Scene && m_AnimatorEntityUUID.IsValid())
	{
		const Entity animatorEntity = m_Scene->TryGetEntityByUUID(m_AnimatorEntityUUID);
		if (animatorEntity && animatorEntity.HasComponent<AnimatorComponent>())
		{
			auto& animator = animatorEntity.GetComponent<AnimatorComponent>();
			if (m_SelectedClipIndex >= 0 &&
				static_cast<size_t>(m_SelectedClipIndex) < animator.LoadedClips.size())
			{
				animator.LoadedClips[static_cast<size_t>(m_SelectedClipIndex)] = nullptr;
			}
			animator.PlaybackRevision++;
		}
	}

	Log::CoreInfo("Saved animation markers/events for {}", m_ClipSource.SourcePath.string());
	return true;
}

void AnimationEditorPanel::RevertMeta()
{
	LoadMeta();
}

void AnimationEditorPanel::EnsureClipEntry()
{
	if (!m_ClipSource.Valid)
		return;

	m_ClipMeta.try_emplace(m_ClipSource.ClipKey);
}

AnimationClipMetaEntry* AnimationEditorPanel::GetEditingClip()
{
	if (!m_ClipSource.Valid)
		return nullptr;

	const auto it = m_ClipMeta.find(m_ClipSource.ClipKey);
	return it != m_ClipMeta.end() ? &it->second : nullptr;
}

void AnimationEditorPanel::DrawToolbar()
{
	if (!m_Scene)
	{
		ImGui::TextDisabled("Open a scene to edit animation events.");
		return;
	}

	if (!m_AnimatorEntityUUID.IsValid())
	{
		ImGui::TextDisabled("Select an entity with an AnimatorComponent, or a child/skeleton of one.");
		return;
	}

	const Entity animatorEntity = m_Scene->TryGetEntityByUUID(m_AnimatorEntityUUID);
	if (!animatorEntity || !animatorEntity.HasComponent<AnimatorComponent>())
	{
		ImGui::TextDisabled("The selected animator is no longer valid.");
		return;
	}

	auto& animator = animatorEntity.GetComponent<AnimatorComponent>();

	ImGui::TextDisabled("Animator: %s", std::string(animatorEntity.GetName()).c_str());
	ImGui::SameLine();

	const std::string clipLabel = GetClipLabel(animator, m_SelectedClipIndex);
	if (ImGui::BeginCombo("Clip", clipLabel.c_str()))
	{
		for (int32_t i = 0; i < static_cast<int32_t>(animator.AnimationClips.size()); ++i)
		{
			ImGui::PushID(i);
			const bool selected = i == m_SelectedClipIndex;
			if (ImGui::Selectable(GetClipLabel(animator, i).c_str(), selected))
			{
				m_SelectedClipIndex = i;
				m_LoadedClipHandle = AssetHandle::Invalid();
				animator.ActiveClip = i;
				animator.CurrentTime = 0.0f;
				animator.PlaybackRevision++;
			}
			if (selected)
				ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	bool playing = animator.IsPlaying;
	if (ImGui::Checkbox("Playing", &playing))
		animator.IsPlaying = playing;

	const float duration = std::max(m_ClipSource.Duration, 0.001f);
	float time = std::clamp(animator.CurrentTime, 0.0f, duration);
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::SliderFloat("Time", &time, 0.0f, duration, "%.3f s"))
	{
		animator.CurrentTime = time;
		animator.PlaybackRevision++;
	}

	ImGui::SameLine();
	if (ImGui::Button("Restart"))
	{
		animator.CurrentTime = 0.0f;
		animator.IsPlaying = true;
		animator.PlaybackRevision++;
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop"))
	{
		animator.CurrentTime = 0.0f;
		animator.IsPlaying = false;
		animator.PlaybackRevision++;
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140.0f);
	ImGui::DragFloat("Timeline Zoom", &m_PixelsPerSecond, 5.0f, 60.0f, 2000.0f, "%.0f px/s");

	ImGui::Separator();

	ImGui::BeginDisabled(!m_CanEdit || !m_ClipSource.Valid);
	if (ImGui::Button("Add Event"))
		AddEvent(false);
	ImGui::SameLine();
	if (ImGui::Button("Add Duration Event"))
		AddEvent(true);
	ImGui::SameLine();
	if (ImGui::Button("Add Marker"))
		AddMarker();
	ImGui::SameLine();
	ImGui::BeginDisabled(!m_Dirty);
	if (ImGui::Button("Save"))
		SaveMeta();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Revert"))
		RevertMeta();
	ImGui::EndDisabled();

	if (m_Dirty)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Unsaved changes");
	}

	if (!m_CanEdit)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Editing is only available in Edit mode");
	}

	if (m_ClipSource.Valid)
	{
		ImGui::TextDisabled("%s  |  %s",
		                    m_ClipSource.SourcePath.filename().string().c_str(),
		                    m_ClipSource.ClipKey.c_str());
	}
	else if (!m_SourceError.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "%s", m_SourceError.c_str());
	}
}

void AnimationEditorPanel::DrawTimeline(const float duration)
{
	AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip)
		return;

	const Entity animatorEntity = m_Scene ? m_Scene->TryGetEntityByUUID(m_AnimatorEntityUUID) : Entity{};
	if (!animatorEntity || !animatorEntity.HasComponent<AnimatorComponent>())
		return;

	const auto& animator = animatorEntity.GetComponent<AnimatorComponent>();
	const float safeDuration = std::max(duration, 0.1f);
	const float pixelsPerSecond = std::max(m_PixelsPerSecond, 10.0f);

	// Build one lane per distinct event name. Markers get their own final lane.
	std::vector<std::string> laneNames;
	std::map<std::string, int32_t> laneByName;
	std::vector<int32_t> eventLane(clip->Events.size(), 0);

	for (size_t eventIndex = 0; eventIndex < clip->Events.size(); ++eventIndex)
	{
		const std::string laneName = clip->Events[eventIndex].Name.empty()
			? "(unnamed)"
			: clip->Events[eventIndex].Name;

		auto [it, inserted] = laneByName.emplace(laneName, static_cast<int32_t>(laneNames.size()));
		if (inserted)
			laneNames.push_back(laneName);

		eventLane[eventIndex] = it->second;
	}

	const int32_t laneCount = std::max<int32_t>(1, static_cast<int32_t>(laneNames.size()));
	const int32_t markerLane = laneCount;

	constexpr float rulerHeight = 24.0f;
	constexpr float rowHeight = 22.0f;
	constexpr float labelWidth = 120.0f;
	constexpr float bottomPadding = 8.0f;

	const float canvasWidth = labelWidth + safeDuration * pixelsPerSecond + 32.0f;
	const float canvasHeight =
		rulerHeight + (laneCount + 1) * rowHeight + bottomPadding;

	ImGui::BeginChild("##AnimationTimelineScroll", ImVec2(0.0f, 300.0f), false,
	                  ImGuiWindowFlags_HorizontalScrollbar);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float timeOriginX = origin.x + labelWidth;
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	// Reserve the timeline content size without creating an item that would compete with the
	// event/marker buttons. Scrubbing is handled after all items are submitted.
	ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));

	const ImGuiIO& io = ImGui::GetIO();

	// Background and ruler.
	drawList->AddRectFilled(
		ImVec2(origin.x, origin.y),
		ImVec2(origin.x + canvasWidth, origin.y + canvasHeight),
		IM_COL32(26, 26, 30, 255));

	static constexpr float tickSteps[] = {
		0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f,
	};

	float tickStep = tickSteps[std::size(tickSteps) - 1];
	for (const float step : tickSteps)
	{
		if (step * pixelsPerSecond >= 65.0f)
		{
			tickStep = step;
			break;
		}
	}

	for (float time = 0.0f; time <= safeDuration + 0.0001f; time += tickStep)
	{
		const float x = TimeToX(time, timeOriginX, pixelsPerSecond);
		drawList->AddLine(ImVec2(x, origin.y + rulerHeight - 6.0f),
		                  ImVec2(x, origin.y + canvasHeight),
		                  IM_COL32(55, 55, 62, 255));

		char label[32];
		std::snprintf(label, sizeof(label), "%.2f", time);
		drawList->AddText(ImVec2(x + 3.0f, origin.y + 3.0f),
		                  IM_COL32(170, 170, 180, 255), label);
	}

	// Lane rows and labels.
	for (int32_t lane = 0; lane <= laneCount; ++lane)
	{
		const float rowY = origin.y + rulerHeight + static_cast<float>(lane) * rowHeight;
		const ImU32 rowColor = (lane % 2 == 0)
			? IM_COL32(34, 34, 39, 255)
			: IM_COL32(38, 38, 44, 255);

		drawList->AddRectFilled(
			ImVec2(origin.x, rowY),
			ImVec2(origin.x + canvasWidth, rowY + rowHeight),
			rowColor);

		const char* laneLabel = lane == markerLane
			? "Markers"
			: (lane < static_cast<int32_t>(laneNames.size()) ? laneNames[lane].c_str() : "Events");

		drawList->AddText(ImVec2(origin.x + 5.0f, rowY + 3.0f),
		                  IM_COL32(190, 190, 200, 255), laneLabel);
	}

	// Event bars.
	for (size_t eventIndex = 0; eventIndex < clip->Events.size(); ++eventIndex)
	{
		AnimationEventFile& event = clip->Events[eventIndex];
		const float rowY = origin.y + rulerHeight +
			static_cast<float>(eventLane[eventIndex]) * rowHeight;
		const bool selected = static_cast<int32_t>(eventIndex) == m_SelectedEventIndex;

		ImGui::PushID(static_cast<int>(eventIndex));

		if (event.Duration <= 0.0f)
		{
			const float centerX = TimeToX(event.Time, timeOriginX, pixelsPerSecond);
			const float centerY = rowY + rowHeight * 0.5f;
			const ImU32 color = selected
				? IM_COL32(255, 220, 110, 255)
				: IM_COL32(230, 190, 80, 255);

			drawList->AddQuadFilled(
				ImVec2(centerX, centerY - 6.0f),
				ImVec2(centerX + 6.0f, centerY),
				ImVec2(centerX, centerY + 6.0f),
				ImVec2(centerX - 6.0f, centerY),
				color);

			drawList->AddText(ImVec2(centerX + 9.0f, rowY + 3.0f),
			                  IM_COL32(225, 225, 230, 255),
			                  event.Name.empty() ? "(unnamed)" : event.Name.c_str());

			ImGui::SetCursorScreenPos(ImVec2(centerX - 7.0f, centerY - 7.0f));
			ImGui::InvisibleButton("##PointEvent", ImVec2(14.0f, 14.0f));
			if (ImGui::IsItemClicked())
				m_SelectedEventIndex = static_cast<int32_t>(eventIndex);

			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				event.Time = std::clamp(
					event.Time + io.MouseDelta.x / pixelsPerSecond, 0.0f, safeDuration);
				m_Dirty = true;
			}
		}
		else
		{
			const float x0 = TimeToX(event.Time, timeOriginX, pixelsPerSecond);
			const float x1 = TimeToX(event.Time + event.Duration, timeOriginX, pixelsPerSecond);
			const float width = std::max(x1 - x0, 8.0f);
			const float barButtonWidth = std::max(width - 7.0f, 1.0f);
			const ImU32 fill = selected
				? IM_COL32(120, 200, 255, 230)
				: IM_COL32(80, 150, 210, 210);

			drawList->AddRectFilled(
				ImVec2(x0, rowY + 2.0f),
				ImVec2(x0 + width, rowY + rowHeight - 2.0f),
				fill,
				3.0f);
			drawList->AddRect(
				ImVec2(x0, rowY + 2.0f),
				ImVec2(x0 + width, rowY + rowHeight - 2.0f),
				selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(30, 70, 110, 255),
				3.0f);
			drawList->AddText(ImVec2(x0 + 5.0f, rowY + 3.0f),
			                  IM_COL32(240, 245, 250, 255),
			                  event.Name.empty() ? "(unnamed)" : event.Name.c_str());

			ImGui::SetCursorScreenPos(ImVec2(x0, rowY + 2.0f));
			ImGui::InvisibleButton("##DurationEvent", ImVec2(barButtonWidth, rowHeight - 4.0f));
			if (ImGui::IsItemClicked())
				m_SelectedEventIndex = static_cast<int32_t>(eventIndex);

			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				event.Time = std::clamp(
					event.Time + io.MouseDelta.x / pixelsPerSecond, 0.0f, safeDuration);
				event.Duration = std::clamp(
					event.Duration, 0.0f, std::max(0.0f, safeDuration - event.Time));
				m_Dirty = true;
			}

			// Right edge resize handle, adjacent to (not overlapping) the bar.
			ImGui::SetCursorScreenPos(ImVec2(x0 + width - 7.0f, rowY + 2.0f));
			ImGui::InvisibleButton("##DurationResize", ImVec2(7.0f, rowHeight - 4.0f));
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				event.Duration = std::clamp(
					event.Duration + io.MouseDelta.x / pixelsPerSecond,
					0.0f,
					std::max(0.0f, safeDuration - event.Time));
				m_Dirty = true;
			}
		}

		ImGui::PopID();
	}

	// Markers.
	for (size_t markerIndex = 0; markerIndex < clip->Markers.size(); ++markerIndex)
	{
		AnimationMarkerFile& marker = clip->Markers[markerIndex];
		const float rowY = origin.y + rulerHeight + static_cast<float>(markerLane) * rowHeight;
		const float centerX = TimeToX(marker.Time, timeOriginX, pixelsPerSecond);
		const float centerY = rowY + rowHeight * 0.5f;
		const bool selected = static_cast<int32_t>(markerIndex) == m_SelectedMarkerIndex;

		ImGui::PushID(static_cast<int>(markerIndex) + 100000);
		drawList->AddQuadFilled(
			ImVec2(centerX, centerY - 6.0f),
			ImVec2(centerX + 6.0f, centerY),
			ImVec2(centerX, centerY + 6.0f),
			ImVec2(centerX - 6.0f, centerY),
			selected ? IM_COL32(255, 180, 255, 255) : IM_COL32(200, 110, 220, 255));

		drawList->AddText(ImVec2(centerX + 9.0f, rowY + 3.0f),
		                  IM_COL32(225, 225, 230, 255),
		                  marker.Name.empty() ? "(unnamed)" : marker.Name.c_str());

		ImGui::SetCursorScreenPos(ImVec2(centerX - 7.0f, centerY - 7.0f));
		ImGui::InvisibleButton("##Marker", ImVec2(14.0f, 14.0f));
		if (ImGui::IsItemClicked())
			m_SelectedMarkerIndex = static_cast<int32_t>(markerIndex);

		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			marker.Time = std::clamp(
				marker.Time + io.MouseDelta.x / pixelsPerSecond, 0.0f, safeDuration);
			m_Dirty = true;
		}

		ImGui::PopID();
	}

	// Scrub only when the click did not land on an event/marker item.
	const bool windowHovered = ImGui::IsWindowHovered();
	const bool itemHovered = ImGui::IsAnyItemHovered();
	if (windowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !itemHovered &&
		io.MousePos.x >= timeOriginX)
	{
		m_Scrubbing = true;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		m_Scrubbing = false;

	if (m_Scrubbing)
	{
		const float clampedMouseX = std::max(io.MousePos.x, timeOriginX);
		const float newTime = std::clamp(
			XToTime(clampedMouseX, timeOriginX, pixelsPerSecond), 0.0f, safeDuration);

		AnimatorComponent& mutableAnimator = animatorEntity.GetComponent<AnimatorComponent>();
		mutableAnimator.CurrentTime = newTime;
		mutableAnimator.PlaybackRevision++;
	}

	// Playhead.
	const float playheadTime = std::clamp(animator.CurrentTime, 0.0f, safeDuration);
	const float playheadX = TimeToX(playheadTime, timeOriginX, pixelsPerSecond);
	drawList->AddLine(
		ImVec2(playheadX, origin.y + rulerHeight),
		ImVec2(playheadX, origin.y + canvasHeight),
		IM_COL32(235, 70, 70, 255),
		1.5f);
	drawList->AddTriangleFilled(
		ImVec2(playheadX - 5.0f, origin.y + rulerHeight - 9.0f),
		ImVec2(playheadX + 5.0f, origin.y + rulerHeight - 9.0f),
		ImVec2(playheadX, origin.y + rulerHeight),
		IM_COL32(235, 70, 70, 255));

	ImGui::EndChild();
}

void AnimationEditorPanel::DrawEventInspector()
{
	AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip)
		return;

	if (m_SelectedEventIndex < 0 ||
		m_SelectedEventIndex >= static_cast<int32_t>(clip->Events.size()))
	{
		ImGui::TextDisabled("Select an event in the timeline to edit it.");
		return;
	}

	AnimationEventFile& event = clip->Events[static_cast<size_t>(m_SelectedEventIndex)];
	ImGui::PushID(m_SelectedEventIndex);

	ImGui::SeparatorText("Event");

	ImGui::BeginDisabled(!m_CanEdit);
	if (InputString("Name", event.Name))
		m_Dirty = true;
	if (ImGui::DragFloat("Time", &event.Time, 0.01f, 0.0f, std::max(m_ClipSource.Duration, 0.0f),
	                     "%.3f s"))
	{
		event.Time = std::clamp(event.Time, 0.0f, std::max(m_ClipSource.Duration, 0.0f));
		m_Dirty = true;
	}
	if (ImGui::DragFloat("Duration", &event.Duration, 0.01f, 0.0f,
	                     std::max(m_ClipSource.Duration, 0.0f), "%.3f s"))
	{
		event.Duration = std::clamp(event.Duration, 0.0f,
		                            std::max(0.0f, m_ClipSource.Duration - event.Time));
		m_Dirty = true;
	}
	if (InputMultiline("Payload", event.Payload))
		m_Dirty = true;
	if (ImGui::Checkbox("Fire In Editor", &event.FireInEditor))
		m_Dirty = true;
	if (ImGui::SliderFloat("Min Trigger Weight", &event.MinTriggerWeight, 0.0f, 1.0f))
		m_Dirty = true;

	int flags = static_cast<int>(event.Flags);
	if (ImGui::InputInt("Flags", &flags))
	{
		event.Flags = static_cast<uint32_t>(flags);
		m_Dirty = true;
	}

	ImGui::SeparatorText("Parameters");

	int32_t parameterToRemove = -1;
	for (int32_t parameterIndex = 0;
	     parameterIndex < static_cast<int32_t>(event.Parameters.size());
	     ++parameterIndex)
	{
		ImGui::PushID(parameterIndex);
		AnimationEventParameterFile& parameter =
			event.Parameters[static_cast<size_t>(parameterIndex)];

		if (InputString("Name", parameter.Name))
			m_Dirty = true;

		const char* currentType = ParameterTypeLabel(parameter.Type);
		if (ImGui::BeginCombo("Type", currentType))
		{
			for (int typeIndex = 0;
			     typeIndex <= static_cast<int>(AnimationEventParameterTypeFile::Asset);
			     ++typeIndex)
			{
				const auto type = static_cast<AnimationEventParameterTypeFile>(typeIndex);
				const bool selected = parameter.Type == type;
				if (ImGui::Selectable(ParameterTypeLabel(type), selected))
				{
					parameter.Type = type;
					m_Dirty = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		DrawParameterEditor(parameter, parameterIndex);

		if (ImGui::SmallButton("Remove Parameter"))
			parameterToRemove = parameterIndex;

		ImGui::Separator();
		ImGui::PopID();
	}

	if (parameterToRemove >= 0)
	{
		event.Parameters.erase(event.Parameters.begin() + parameterToRemove);
		m_Dirty = true;
	}

	if (ImGui::Button("Add Parameter"))
	{
		AnimationEventParameterFile parameter{};
		parameter.Name = "Value";
		parameter.Type = AnimationEventParameterTypeFile::Float;
		event.Parameters.push_back(std::move(parameter));
		m_Dirty = true;
	}

	ImGui::Separator();
	if (ImGui::Button("Delete Event"))
		DeleteSelectedEvent();
	ImGui::EndDisabled();

	ImGui::PopID();
}

void AnimationEditorPanel::DrawParameterEditor(AnimationEventParameterFile& parameter,
                                               const int32_t parameterIndex)
{
	(void)parameterIndex;

	switch (parameter.Type)
	{
	case AnimationEventParameterTypeFile::Bool:
		if (ImGui::Checkbox("Value", &parameter.BoolValue))
			m_Dirty = true;
		break;

	case AnimationEventParameterTypeFile::Int:
		if (ImGui::DragInt("Value", &parameter.IntValue))
			m_Dirty = true;
		break;

	case AnimationEventParameterTypeFile::Float:
		if (ImGui::DragFloat("Value", &parameter.FloatValue, 0.01f))
			m_Dirty = true;
		break;

	case AnimationEventParameterTypeFile::Vector3:
		if (ImGui::DragFloat3("Value", glm::value_ptr(parameter.VectorValue), 0.01f))
			m_Dirty = true;
		break;

	case AnimationEventParameterTypeFile::String:
		if (InputString("Value", parameter.StringValue))
			m_Dirty = true;
		break;

	case AnimationEventParameterTypeFile::Asset:
	{
		AssetHandle handle(parameter.AssetValue);
		if (AssetPicker::DrawAnyAssetHandle("Value", handle))
		{
			parameter.AssetValue = handle;
			m_Dirty = true;
		}
		break;
	}
	}
}

void AnimationEditorPanel::DrawMarkerInspector()
{
	AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip)
		return;

	if (m_SelectedMarkerIndex < 0 ||
		m_SelectedMarkerIndex >= static_cast<int32_t>(clip->Markers.size()))
	{
		return;
	}

	AnimationMarkerFile& marker = clip->Markers[static_cast<size_t>(m_SelectedMarkerIndex)];
	ImGui::PushID(static_cast<int>(m_SelectedMarkerIndex) + 100000);

	ImGui::SeparatorText("Marker");

	ImGui::BeginDisabled(!m_CanEdit);
	if (InputString("Marker Name", marker.Name))
		m_Dirty = true;
	if (ImGui::DragFloat("Marker Time", &marker.Time, 0.01f, 0.0f,
	                     std::max(m_ClipSource.Duration, 0.0f), "%.3f s"))
	{
		marker.Time = std::clamp(marker.Time, 0.0f, std::max(m_ClipSource.Duration, 0.0f));
		m_Dirty = true;
	}

	if (ImGui::Button("Delete Marker"))
		DeleteSelectedMarker();
	ImGui::EndDisabled();

	ImGui::PopID();
}

void AnimationEditorPanel::DrawValidation()
{
	const AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip)
		return;

	int32_t issues = 0;
	for (const AnimationEventFile& event : clip->Events)
	{
		if (event.Name.empty())
			++issues;
		if (event.Time < 0.0f || (m_ClipSource.Duration > 0.0f && event.Time > m_ClipSource.Duration))
			++issues;
		if (event.Duration > 0.0f && m_ClipSource.Duration > 0.0f &&
			event.Time + event.Duration > m_ClipSource.Duration + 1.0e-4f)
		{
			++issues;
		}
	}

	if (issues == 0)
	{
		ImGui::TextDisabled("No validation issues.");
		return;
	}

	ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%d validation issue(s)", issues);

	for (size_t eventIndex = 0; eventIndex < clip->Events.size(); ++eventIndex)
	{
		const AnimationEventFile& event = clip->Events[eventIndex];
		const std::string label = event.Name.empty()
			? "Event " + std::to_string(eventIndex)
			: event.Name;

		if (event.Name.empty())
			ImGui::BulletText("%s has an empty name", label.c_str());
		if (event.Time < 0.0f || (m_ClipSource.Duration > 0.0f && event.Time > m_ClipSource.Duration))
			ImGui::BulletText("%s starts outside the clip", label.c_str());
		if (event.Duration > 0.0f && m_ClipSource.Duration > 0.0f &&
			event.Time + event.Duration > m_ClipSource.Duration + 1.0e-4f)
		{
			ImGui::BulletText("%s ends outside the clip", label.c_str());
		}
	}
}

void AnimationEditorPanel::AddEvent(const bool duration)
{
	if (!m_CanEdit || !m_ClipSource.Valid)
		return;

	AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip)
		return;

	const Entity animatorEntity = m_Scene->TryGetEntityByUUID(m_AnimatorEntityUUID);
	if (!animatorEntity || !animatorEntity.HasComponent<AnimatorComponent>())
		return;

	const auto& animator = animatorEntity.GetComponent<AnimatorComponent>();
	const float clipDuration = std::max(m_ClipSource.Duration, 0.0f);
	const float time = std::clamp(animator.CurrentTime, 0.0f, clipDuration);

	AnimationEventFile event{};
	event.Time = time;
	event.Duration = duration
		? std::min(0.2f, std::max(0.0f, clipDuration - time))
		: 0.0f;
	event.Name = duration ? "Window" : "Event";

	clip->Events.push_back(std::move(event));
	m_SelectedEventIndex = static_cast<int32_t>(clip->Events.size() - 1);
	m_SelectedMarkerIndex = -1;
	m_Dirty = true;
}

void AnimationEditorPanel::AddMarker()
{
	if (!m_CanEdit || !m_ClipSource.Valid)
		return;

	AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip)
		return;

	const Entity animatorEntity = m_Scene->TryGetEntityByUUID(m_AnimatorEntityUUID);
	if (!animatorEntity || !animatorEntity.HasComponent<AnimatorComponent>())
		return;

	const auto& animator = animatorEntity.GetComponent<AnimatorComponent>();
	AnimationMarkerFile marker{};
	marker.Name = "Marker";
	marker.Time = std::clamp(animator.CurrentTime, 0.0f, std::max(m_ClipSource.Duration, 0.0f));

	clip->Markers.push_back(std::move(marker));
	m_SelectedMarkerIndex = static_cast<int32_t>(clip->Markers.size() - 1);
	m_SelectedEventIndex = -1;
	m_Dirty = true;
}

void AnimationEditorPanel::DeleteSelectedEvent()
{
	if (!m_CanEdit)
		return;

	AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip || m_SelectedEventIndex < 0 ||
		m_SelectedEventIndex >= static_cast<int32_t>(clip->Events.size()))
	{
		return;
	}

	clip->Events.erase(clip->Events.begin() + m_SelectedEventIndex);
	m_SelectedEventIndex = std::min<int32_t>(
		m_SelectedEventIndex, static_cast<int32_t>(clip->Events.size()) - 1);
	m_Dirty = true;
}

void AnimationEditorPanel::DeleteSelectedMarker()
{
	if (!m_CanEdit)
		return;

	AnimationClipMetaEntry* clip = GetEditingClip();
	if (!clip || m_SelectedMarkerIndex < 0 ||
		m_SelectedMarkerIndex >= static_cast<int32_t>(clip->Markers.size()))
	{
		return;
	}

	clip->Markers.erase(clip->Markers.begin() + m_SelectedMarkerIndex);
	m_SelectedMarkerIndex = std::min<int32_t>(
		m_SelectedMarkerIndex, static_cast<int32_t>(clip->Markers.size()) - 1);
	m_Dirty = true;
}

float AnimationEditorPanel::TimeToX(const float time, const float originX,
                                    const float pixelsPerSecond)
{
	return originX + time * pixelsPerSecond;
}

float AnimationEditorPanel::XToTime(const float x, const float originX,
                                    const float pixelsPerSecond)
{
	return pixelsPerSecond > 0.0f ? (x - originX) / pixelsPerSecond : 0.0f;
}

}
