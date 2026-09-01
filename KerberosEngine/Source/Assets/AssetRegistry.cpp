#include "AssetRegistry.hpp"
#include "Core/Core.hpp"
#include "Profiling/Instrumentor.hpp"

#include <algorithm>
#include <ranges>
#include <cctype>

import Kerberos;

namespace Kerberos
{
	//static std::mutex s_AssetRegistryMutex;

	AssetMetadata& AssetRegistry::operator[](const AssetHandle handle)
	{
		return m_Registry[handle];
	}

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
		m_Registry.erase(it);
		return 1;
	}

	void AssetRegistry::Clear()
	{
		m_Registry.clear();
	}

	void AssetRegistry::Add(const AssetHandle handle, const AssetMetadata& metadata)
	{
		m_Registry[handle] = metadata;
	}

	bool AssetRegistry::ContainsPath(const std::filesystem::path& path) const
	{
        KBR_PROFILE_FUNCTION();

		const auto normalized = NormalizePath(path);
		for (const auto& metadata : m_Registry | std::views::values)
			if (NormalizePath(metadata.Filepath) == normalized)
				return true;
		return false;
	}

	AssetHandle AssetRegistry::GetHandle(const std::filesystem::path& path) const
	{
        KBR_PROFILE_FUNCTION();

		const auto normalized = NormalizePath(path);
		for (const auto& [handle, metadata] : m_Registry)
			if (NormalizePath(metadata.Filepath) == normalized)
				return handle;

		Log::CoreError("AssetRegistry::GetHandle - no handle found for path: {}", path.string());
		return AssetHandle::Invalid();
	}

	std::string AssetRegistry::NormalizePath(const std::filesystem::path& path)
	{
        KBR_PROFILE_FUNCTION();

		std::string value = path.lexically_normal().generic_string();
		std::ranges::transform(value, value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}
}