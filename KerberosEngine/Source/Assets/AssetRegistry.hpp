#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>
#include <vector>

#include "Asset.hpp"
#include "AssetMetadata.hpp"

#include <string>
namespace Kerberos
{
	class AssetRegistry
	{
	public:
		static constexpr size_t TypeCount = static_cast<size_t>(AssetType::Count);

		AssetMetadata& Get(AssetHandle handle);
		const AssetMetadata& Get(AssetHandle handle) const;
		size_t Count() const { return m_Registry.size(); }
		bool Contains(AssetHandle handle) const;
		size_t Remove(AssetHandle handle);
		void Clear();

		void Add(AssetHandle handle, const AssetMetadata& metadata);

		/**
		 * Returns every registered asset of the given type.
		 *
		 * The result is a view into a dense per-type index maintained by Add/Remove/Clear,
		 * so this is O(1) and does not scan the registry. The span is valid until the
		 * registry is mutated.
		 */
		std::span<const AssetHandle> GetAssetsOfType(AssetType type) const;

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
		struct TypeSlot
		{
			AssetType Type = AssetType::Texture2D;
			uint32_t Slot = 0;
		};

		void AddToTypeIndex(AssetHandle handle, AssetType type);
		void RemoveFromTypeIndex(AssetHandle handle, AssetType type);

		static std::string NormalizePath(const std::filesystem::path& path);
		std::map<AssetHandle, AssetMetadata> m_Registry;

		// Dense per-type lists used by GetAssetsOfType.
		std::array<std::vector<AssetHandle>, TypeCount> m_HandlesByType;

		// Handle -> (type, slot in m_HandlesByType) for O(1) removal and updates.
		std::unordered_map<AssetHandle, TypeSlot> m_TypeSlots;
	};
}
