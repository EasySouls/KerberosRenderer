#include "AssetPicker.hpp"

#include "AssetConstants.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Project/Project.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kerberos
{
	namespace
	{
		struct PickerEntry
		{
			AssetHandle Handle = AssetHandle::Invalid();
			std::string Label;
			std::string SearchKey;
		};

		struct PickerCache
		{
			size_t RegistryCount = static_cast<size_t>(-1);
			std::vector<PickerEntry> Entries;
		};

        std::unordered_map<size_t, PickerCache>& GetCaches()
        {
            static std::unordered_map<size_t, PickerCache> caches;
            return caches;
        }

        std::unordered_map<ImGuiID, std::array<char, 256>>& GetFilters()
        {
            static std::unordered_map<ImGuiID, std::array<char, 256>> filters;
            return filters;
        }

		constexpr size_t AnyAssetCacheKey = static_cast<size_t>(AssetType::Count) + 1;

		std::string ToLowerCopy(const std::string_view text)
		{
			std::string result(text);
			std::ranges::transform(result, result.begin(), [](const unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return result;
		}

		std::string AssetDisplayLabel(const AssetMetadata& metadata, const AssetHandle handle)
		{
			std::string label;
			if (!metadata.Filepath.empty())
				label = metadata.Filepath.generic_string();
			else if (!metadata.LibraryPath.empty())
				label = metadata.LibraryPath.generic_string();
			else if (!metadata.SubAssetKey.empty())
				label = metadata.SubAssetKey;
			else
				label = std::format("Asset {}", static_cast<uint64_t>(handle));

			if (!metadata.SubAssetKey.empty())
				label += "  [" + metadata.SubAssetKey + "]";
			return label;
		}

		PickerCache& GetCache(const std::optional<AssetType> type)
		{
			const size_t cacheKey = type ? static_cast<size_t>(*type) : AnyAssetCacheKey;
			PickerCache& cache = GetCaches()[cacheKey];

			const Ref<AssetManagerBase> manager = Project::GetActive()
				? Project::GetActive()->GetAssetManager() : nullptr;
			if (!manager)
				return cache;

			const AssetRegistry& registry = manager->GetAssetRegistry();
			if (cache.RegistryCount == registry.Count())
				return cache;

			cache.RegistryCount = registry.Count();
			cache.Entries.clear();

			const auto addHandle = [&](const AssetHandle handle)
			{
				if (!registry.Contains(handle))
					return;

				const AssetMetadata& metadata = registry.Get(handle);
				PickerEntry entry{
					.Handle = handle,
					.Label = AssetDisplayLabel(metadata, handle),
					.SearchKey = ""
				};
				if (!type)
					entry.Label = std::string("[") + std::string(AssetTypeToString(metadata.Type)) + "] " + entry.Label;
				entry.SearchKey = ToLowerCopy(std::format("{} {} {} {} {}",
					entry.Label,
					metadata.Filepath.generic_string(),
					metadata.LibraryPath.generic_string(),
					metadata.SubAssetKey,
					AssetTypeToString(metadata.Type)));
				cache.Entries.emplace_back(std::move(entry));
			};

			if (type)
			{
				for (const AssetHandle handle : manager->GetAssetsOfType(*type))
					addHandle(handle);
			}
			else
			{
				for (size_t typeIndex = 0; typeIndex < static_cast<size_t>(AssetType::Count); ++typeIndex)
				{
					for (const AssetHandle handle : manager->GetAssetsOfType(static_cast<AssetType>(typeIndex)))
						addHandle(handle);
				}
			}

			std::ranges::sort(cache.Entries, [](const PickerEntry& left, const PickerEntry& right)
			{
				return ToLowerCopy(left.Label) < ToLowerCopy(right.Label);
			});

			return cache;
		}

		bool DrawInternal(const char* label, const std::optional<AssetType> type, AssetHandle& handle,
			const char* hint, const bool allowNone)
		{
			const auto project = Project::GetActive();
			const Ref<AssetManagerBase> manager = project ? project->GetAssetManager() : nullptr;
			if (!manager)
				return false;

			const AssetRegistry& registry = manager->GetAssetRegistry();
			PickerCache& cache = GetCache(type);

			std::string currentLabel = allowNone ? "None" : "Select asset...";
			if (handle.IsValid())
			{
				if (registry.Contains(handle))
					currentLabel = AssetDisplayLabel(registry.Get(handle), handle);
				else
					currentLabel = "Missing asset";
			}

			ImGui::PushID(label);
			ImGui::TextUnformatted(label);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);

			const ImGuiID filterId = ImGui::GetID("##AssetPickerFilter");
			std::array<char, 256>& filterBuffer = GetFilters()[filterId];

			bool changed = false;
			const bool comboOpen = ImGui::BeginCombo("##AssetPicker", currentLabel.c_str());

			// Keep the combo itself as a drop target so existing drag/drop habits
			// continue to work alongside the searchable list. While the popup is
			// open we are inside its window, so only register the target on the
			// closed combo button.
			if (!comboOpen && ImGui::BeginDragDropTarget())
			{
				AssetHandle dropped = AssetHandle::Invalid();
				if (type ? AssetPicker::AcceptDragDrop(*type, dropped) : AssetPicker::AcceptAnyDragDrop(dropped))
				{
					handle = dropped;
					changed = true;
				}
				ImGui::EndDragDropTarget();
			}

			if (comboOpen)
			{
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputTextWithHint("##AssetPickerSearch",
					hint ? hint : "Search assets...", filterBuffer.data(), filterBuffer.size());

				const std::string filter = ToLowerCopy(filterBuffer.data());
				std::vector<size_t> visibleEntries;
				visibleEntries.reserve(cache.Entries.size());
				for (size_t i = 0; i < cache.Entries.size(); ++i)
				{
					if (filter.empty() || cache.Entries[i].SearchKey.find(filter) != std::string::npos)
						visibleEntries.push_back(i);
				}

				if (ImGui::BeginChild("##AssetPickerList", ImVec2(0.0f, 300.0f), ImGuiChildFlags_Borders))
				{
					if (allowNone && ImGui::Selectable("None", !handle.IsValid()))
					{
						handle = AssetHandle::Invalid();
						changed = true;
						ImGui::CloseCurrentPopup();
					}

					if (visibleEntries.empty())
					{
						ImGui::TextDisabled("No matching assets.");
					}
					else
					{
						ImGuiListClipper clipper;
						clipper.Begin(static_cast<int>(visibleEntries.size()));
						while (clipper.Step())
						{
							for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
							{
								const PickerEntry& entry = cache.Entries[visibleEntries[static_cast<size_t>(i)]];
								const bool selected = handle.IsValid() && handle == entry.Handle;
								ImGui::PushID(static_cast<int>(static_cast<uint64_t>(entry.Handle) & 0x7fffffff));
								if (ImGui::Selectable(entry.Label.c_str(), selected))
								{
									if (manager->IsAssetHandleValid(entry.Handle))
									{
										handle = entry.Handle;
										changed = true;
									}
									ImGui::CloseCurrentPopup();
								}
								ImGui::SetItemTooltip("%s", entry.Label.c_str());
								ImGui::PopID();
							}
						}
					}
				}
				ImGui::EndChild();
				ImGui::EndCombo();
			}
			else
			{
				filterBuffer[0] = '\0';
			}

			ImGui::PopID();
			return changed;
		}
	}

	void AssetPicker::InvalidateCache()
	{
		GetCaches().clear();
	}

	const char* AssetPicker::DragPayloadForType(const AssetType type)
	{
		switch (type)
		{
		case AssetType::Texture2D: return assetBrowserTexture;
		case AssetType::TextureCube: return assetBrowserTextureCube;
		case AssetType::Material: return assetBrowserMaterial;
		case AssetType::Mesh: return assetBrowserMesh;
		case AssetType::Scene: return assetBrowserScene;
		case AssetType::Sound: return assetBrowserAudio;
		case AssetType::Prefab: return assetBrowserPrefab;
		case AssetType::Skeleton: return assetBrowserSkeleton;
		case AssetType::SkinnedMesh: return assetBrowserSkinnedMesh;
		case AssetType::Animation: return assetBrowserAnimation;
		default: return assetBrowserItem;
		}
	}

	bool AssetPicker::AcceptDragDrop(const AssetType type, AssetHandle& handle)
	{
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragPayloadForType(type));
		if (!payload)
			return false;

		const AssetHandle dropped = ReadAssetDragPayload(payload->Data, payload->DataSize).Handle;
		if (!dropped.IsValid())
			return false;

		const Ref<AssetManagerBase> manager = Project::GetActive()
			? Project::GetActive()->GetAssetManager() : nullptr;
		if (!manager || !manager->IsAssetHandleValid(dropped))
			return false;

		if (manager->GetAssetType(dropped) != type)
			return false;

		handle = dropped;
		return true;
	}

	bool AssetPicker::AcceptAnyDragDrop(AssetHandle& handle)
	{
		static constexpr const char* payloadTypes[] = {
			assetBrowserTexture,
			assetBrowserTextureCube,
			assetBrowserMaterial,
			assetBrowserMesh,
			assetBrowserScene,
			assetBrowserAudio,
			assetBrowserPrefab,
			assetBrowserSkeleton,
			assetBrowserSkinnedMesh,
			assetBrowserAnimation,
			assetBrowserItem,
		};

		for (const char* payloadType : payloadTypes)
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType);
			if (!payload)
				continue;

			const AssetHandle dropped = ReadAssetDragPayload(payload->Data, payload->DataSize).Handle;
			if (dropped.IsValid())
			{
				handle = dropped;
				return true;
			}
		}
		return false;
	}

	bool AssetPicker::DrawAssetHandle(const char* label, const AssetType type, AssetHandle& handle,
		const char* hint, const bool allowNone)
	{
		return DrawInternal(label, type, handle, hint, allowNone);
	}

	bool AssetPicker::DrawAnyAssetHandle(const char* label, AssetHandle& handle,
		const char* hint, const bool allowNone)
	{
		return DrawInternal(label, std::nullopt, handle, hint, allowNone);
	}
}
