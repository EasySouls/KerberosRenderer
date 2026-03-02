#include "kbrpch.hpp"
#include "AssetRegistry.hpp"
#include <ranges>

namespace Kerberos
{
	//static std::mutex s_AssetRegistryMutex;

	AssetMetadata& AssetRegistry::operator[](const AssetHandle handle)
	{
		return m_Registry[handle];
	}

	AssetMetadata& AssetRegistry::Get(const AssetHandle handle)
	{
		KBR_CORE_ASSERT(m_Registry.contains(handle), "AssetRegistry::Get - registry doesn't contain AssetHandle {}", static_cast<uint64_t>(handle));

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
		return m_Registry.erase(handle);
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
		for (const auto& [Type, Filepath] : m_Registry | std::views::values)
		{
			if (Filepath == path)
			{
				return true;
			}
		}

		return false;
	}

	AssetHandle AssetRegistry::GetHandle(const std::filesystem::path& path) const
	{
		for (const auto& [Handle, Metadata] : m_Registry)
		{
			if (Metadata.Filepath == path)
			{
				return Handle;
			}
		}

		KBR_CORE_ERROR("AssetRegistry::GetHandle - no handle found for path: {}", path.string());
		return AssetHandle::Invalid();
	}
}