#pragma once

#include <map>

#include "Asset.hpp"
#include "AssetMetadata.hpp"

namespace Kerberos
{
	class AssetRegistry
	{
	public:
		AssetMetadata& operator[](AssetHandle handle);
		AssetMetadata& Get(AssetHandle handle);
		const AssetMetadata& Get(AssetHandle handle) const;

		size_t Count() const { return m_Registry.size(); }
		bool Contains(AssetHandle handle) const;
		size_t Remove(AssetHandle handle);
		void Clear();

		void Add(AssetHandle handle, const AssetMetadata& metadata);

		/**
		 * Checks if the registry contains an asset with the given path.
		 * Should be used sparingly, as it requires iterating through the registry.
		 * @param path The path to the asset.
		 * @return true if the asset exists, false otherwise.
		 */
		bool ContainsPath(const std::filesystem::path& path) const;

		/**
		 * Retrieves the handle of an asset by its path.
		 * Should be used sparingly, as it requires iterating through the registry.
		 * @param path The path to the asset.
		 * @return The handle of the asset if it exists, AssetHandle::Invalid() otherwise.
		 */
		AssetHandle GetHandle(const std::filesystem::path& path) const;

		auto begin() { return m_Registry.begin(); }
		auto end() { return m_Registry.end(); }
		auto begin() const { return m_Registry.cbegin(); }
		auto end() const { return m_Registry.cend(); }

	private:
		std::map<AssetHandle, AssetMetadata> m_Registry;
	};
}