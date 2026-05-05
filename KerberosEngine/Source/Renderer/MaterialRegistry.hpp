#pragma once

#include <set>

#include "Material.hpp"
#include "Core/Core.hpp"

#include <unordered_map>
#include <string>
#include <set>

namespace Kerberos 
{
	class MaterialRegistry final
	{
	public:
		MaterialRegistry() = default;
		~MaterialRegistry();

		void Add(const std::string& name, const Material& mat);
		void Add(const std::string& name, const Ref<Material>& mat);
		const Ref<Material>& AddAndRetrieve(const std::string& name, const Material& mat);
		const Ref<Material>& AddAndRetrieve(const std::string& name, const Ref<Material>& mat);

		void SetupDescriptorSets(const vk::DescriptorSetLayout& setLayout);
		void UpdateDescriptorSetsForMaterials(const std::set<Ref<Material>>& set);

		const Ref<Material>& Get(const std::string& name) const;
		Ref<Material>& Get(const std::string& name);

		uint32_t Size() const { return static_cast<uint32_t>(m_Materials.size()); }

		std::unordered_map<std::string, Ref<Material>>::iterator begin() { return m_Materials.begin(); }
		std::unordered_map<std::string, Ref<Material>>::iterator end() { return m_Materials.end(); }

	private:
		void AllocateDescriptorSets(const Ref<Material>& material);
		void InitPlaceholdersIfNeeded();

	private:
		std::unordered_map<std::string, Ref<Material>> m_Materials;

		std::vector<vk::raii::DescriptorPool> m_DescriptorPools;
		uint32_t m_SetsAllocatedInCurrentPool = 0;
		static constexpr uint32_t maxSetsPerPool = 1000;

		vk::DescriptorSetLayout m_SetLayout = nullptr;

		uint32_t m_TexturePerMaterial = 5;

		// TODO: Move from here
		Ref<Texture2D> m_AlbedoPlaceholder;
		Ref<Texture2D> m_NormalPlaceholder;
		Ref<Texture2D> m_RoughnessPlaceholder;
		Ref<Texture2D> m_MetallicPlaceholder;
		Ref<Texture2D> m_AOPlaceholder;
		Ref<Texture2D> m_EmissivePlaceholder;
	};
}