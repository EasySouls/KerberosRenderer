#include "AssetRegistry.hpp"
#include "Core/Core.hpp"
#include "Profiling/Profilers.hpp"

#include <algorithm>
#include <ranges>
#include <cctype>

import Kerberos;

namespace Kerberos
{
	//static std::mutex s_AssetRegistryMutex;

	AssetMetadata& AssetRegistry::Get(const AssetHandle handle)
	{
		KBRAssert(m_Registry.contains(handle), "AssetRegistry::Get - registry doesn't contain AssetHandle {}", static_cast<uint64_t>(handle));

		return m_Registry.at(handle);
	}

	const AssetMetadata& AssetRegistry::Get(const AssetHandle handle) const
	{
		return m_Registry.at(handle);
	}

	bool AssetRegistry::Contains(const AssetHandle handle) const
	{
		return m_Registry.contains(handle);
	}

	size_t AssetRegistry::Remove(const AssetHandle handle)
	{
		const auto it = m_Registry.find(handle);
		if (it == m_Registry.end())
			return 0;

		RemoveFromTypeIndex(handle, it->second.Type);
		m_Registry.erase(it);
		return 1;
	}

	void AssetRegistry::Clear()
	{
		m_Registry.clear();
		for (auto& handles : m_HandlesByType)
			handles.clear();
		m_TypeSlots.clear();
	}

	void AssetRegistry::Add(const AssetHandle handle, const AssetMetadata& metadata)
	{
		const auto it = m_Registry.find(handle);
		if (it == m_Registry.end())
		{
			m_Registry.emplace(handle, metadata);
			AddToTypeIndex(handle, metadata.Type);
			return;
		}

		// Re-adding an existing handle is an upsert. Keep the dense type index in
		// sync when a registry entry changes type.
		if (it->second.Type != metadata.Type)
		{
			RemoveFromTypeIndex(handle, it->second.Type);
			AddToTypeIndex(handle, metadata.Type);
		}
		it->second = metadata;
	}

	std::span<const AssetHandle> AssetRegistry::GetAssetsOfType(const AssetType type) const
	{
		const auto index = static_cast<size_t>(type);
		if (index >= TypeCount)
			return {};
		return std::span<const AssetHandle>(m_HandlesByType[index]);
	}

	void AssetRegistry::AddToTypeIndex(const AssetHandle handle, const AssetType type)
	{
		const auto index = static_cast<size_t>(type);
		if (index >= TypeCount)
			return;

		auto& handles = m_HandlesByType[index];
		const auto slot = static_cast<uint32_t>(handles.size());
		handles.push_back(handle);
		m_TypeSlots[handle] = { .Type = type, .Slot = slot };
	}

	void AssetRegistry::RemoveFromTypeIndex(const AssetHandle handle, const AssetType type)
	{
		const auto index = static_cast<size_t>(type);
		if (index >= TypeCount)
			return;

		auto& handles = m_HandlesByType[index];
		const auto slotIt = m_TypeSlots.find(handle);
		if (slotIt == m_TypeSlots.end() || slotIt->second.Type != type)
		{
			// Defensive repair for an inconsistent entry: fall back to a linear
			// removal so the registry and index cannot diverge permanently.
			std::erase(handles, handle);
			m_TypeSlots.erase(handle);
			return;
		}

		const uint32_t slot = slotIt->second.Slot;
		if (slot >= handles.size() || handles[slot] != handle)
		{
			std::erase(handles, handle);
			m_TypeSlots.erase(handle);
			return;
		}

		const AssetHandle moved = handles.back();
		handles[slot] = moved;
		handles.pop_back();

		if (slot < handles.size())
			m_TypeSlots[moved] = { .Type = type, .Slot = slot };
		m_TypeSlots.erase(handle);
	}

	bool AssetRegistry::ContainsPath(const std::filesystem::path& path) const
	{
        KBR_TRACY_FUNCTION();

		const auto normalized = NormalizePath(path);
		for (const auto& metadata : m_Registry | std::views::values)
			if (NormalizePath(metadata.Filepath) == normalized)
				return true;
		return false;
	}

	AssetHandle AssetRegistry::GetHandle(const std::filesystem::path& path) const
	{
        KBR_TRACY_FUNCTION();

		const auto normalized = NormalizePath(path);
		for (const auto& [handle, metadata] : m_Registry)
			if (NormalizePath(metadata.Filepath) == normalized)
				return handle;

		Log::CoreError("AssetRegistry::GetHandle - no handle found for path: {}", path.string());
		return AssetHandle::Invalid();
	}

	std::string AssetRegistry::NormalizePath(const std::filesystem::path& path)
	{
        KBR_TRACY_FUNCTION();

		std::string value = path.lexically_normal().generic_string();
		std::ranges::transform(value, value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}
}