#pragma once

#include "Material.hpp"
#include "Core/Core.hpp"

#include <unordered_map>
#include <string>
#include <set>

namespace Kerberos 
{
	class TextureManager;

	class MaterialRegistry final
	{
	public:
		MaterialRegistry() = default;
		~MaterialRegistry();

		MaterialRegistry(const MaterialRegistry& other) = default;
		MaterialRegistry(MaterialRegistry&& other) noexcept = default;
		MaterialRegistry& operator=(const MaterialRegistry& other) = default;
		MaterialRegistry& operator=(MaterialRegistry&& other) noexcept = default;

		void Add(const std::string& name, const Material& mat);
		void Add(const std::string& name, const Ref<Material>& mat);
		const Ref<Material>& AddAndRetrieve(const std::string& name, const Material& mat);
		const Ref<Material>& AddAndRetrieve(const std::string& name, const Ref<Material>& mat);

		void SyncWithCurrentMaterials(const std::pmr::set<Ref<Material>>& currentMaterials);
		void ResolveAllMaterialIndices(TextureManager& textureManager);

		const Ref<Material>& Get(const std::string& name) const;
		Ref<Material>& Get(const std::string& name);

		uint32_t Size() const { return static_cast<uint32_t>(m_Materials.size()); }

		std::unordered_map<std::string, Ref<Material>>::iterator begin() { return m_Materials.begin(); }
		std::unordered_map<std::string, Ref<Material>>::iterator end() { return m_Materials.end(); }

	private:
		std::unordered_map<std::string, Ref<Material>> m_Materials;
	};
}