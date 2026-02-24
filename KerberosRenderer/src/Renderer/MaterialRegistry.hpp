#pragma once

#include "Material.hpp"
#include "Core/Core.hpp"

#include <unordered_map>
#include <string>

namespace kbr 
{
	class MaterialRegistry
	{
	public:
		MaterialRegistry() = default;

		void Add(const std::string& name, const Material& mat);
		void Add(const std::string& name, const Ref<Material>& mat);
		const Ref<Material>& AddAndRetrieve(const std::string& name, const Material& mat);
		const Ref<Material>& AddAndRetrieve(const std::string& name, const Ref<Material>& mat);

		void SetupDescriptorSets(const vk::raii::DescriptorSetLayout& setLayout);

		const Ref<Material>& Get(const std::string& name) const;
		Ref<Material>& Get(const std::string& name);

		uint32_t Size() const { return static_cast<uint32_t>(m_Materials.size()); }

		std::unordered_map<std::string, Ref<Material>>::iterator begin() { return m_Materials.begin(); }
		std::unordered_map<std::string, Ref<Material>>::iterator end() { return m_Materials.end(); }

	private:
		void RecreateDescriptorPoolIfNeeded();

	private:
		std::unordered_map<std::string, Ref<Material>> m_Materials;

		vk::raii::DescriptorPool m_TextureDescriptorPool = nullptr;
		uint32_t m_PoolSize = 0;

		uint32_t m_TexturePerMaterial = 5;

		// TODO: Move from here
		Texture2D m_AlbedoPlaceholder;
		Texture2D m_NormalPlaceholder;
	};
}