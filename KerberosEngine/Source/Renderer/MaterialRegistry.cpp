#include "MaterialRegistry.hpp"

#include <ranges>

#include "VulkanContext.hpp"
#include "TextureManager.hpp"

import Kerberos;

namespace Kerberos 
{
	MaterialRegistry::~MaterialRegistry()
	{
		VulkanContext::Get().WaitIdle();

		m_Materials.clear();
	}

	void MaterialRegistry::Add(const std::string& name, const Material& mat) 
	{
		if (m_Materials.contains(name)) {
			Log::CoreError("Material with name {} already exists!", name);
		}

		m_Materials[name] = std::make_shared<Material>(mat);
	}

	void MaterialRegistry::Add(const std::string& name, const Ref<Material>& mat)
	{
		if (m_Materials.contains(name)) {
			Log::CoreError("Material with name {} already exists!", name);
		}

		m_Materials[name] = mat;
	}

	const Ref<Material>& MaterialRegistry::AddAndRetrieve(const std::string& name, const Material& mat) 
	{
		Add(name, mat);

		return m_Materials.at(name);
	}

	const Ref<Material>& MaterialRegistry::AddAndRetrieve(const std::string& name, const Ref<Material>& mat)
	{
		Add(name, mat);

		return m_Materials.at(name);
	}

	void MaterialRegistry::SyncWithCurrentMaterials(const std::pmr::set<Ref<Material>>& currentMaterials)
	{
		// This way of clearing the registry is not ideal, as it will remove all materials that are not in the currentMaterials set, which breaks the engine
		// for example, when assigning the DebugPink material to a mesh, when it doesn't have any.
		// For now, just keep adding any new materials, and do not remove them.
		/*for (auto it = m_Materials.begin(); it != m_Materials.end(); )
		{
			const auto& material = it->second;

			if (currentMaterials.contains(material))
			{
				it = m_Materials.erase(it);
			}
			else
			{
				Add(material->Name, material);

				++it;
			}
		}*/

		for (auto it = currentMaterials.begin(); it != currentMaterials.end(); ++it)
		{
			const auto& material = *it;
			if (!m_Materials.contains(material->Name))
			{
				Add(material->Name, material);
			}
		}
	}

	void MaterialRegistry::ResolveAllMaterialIndices(TextureManager& textureManager)
	{
		for (const auto& material : m_Materials | std::views::values)
		{
			material->ResolveIndices(textureManager);
		}
	}

	const Ref<Material>& MaterialRegistry::Get(const std::string& name) const 
	{
		const auto& mat = m_Materials.at(name);
		if (mat == nullptr) {
			Log::CoreError("Material with name {} doesn't exist in the registry!", name);
		}
		return mat;
	}

	Ref<Material>& MaterialRegistry::Get(const std::string& name) 
	{
		auto& mat = m_Materials[name];
		if (mat == nullptr) {
			Log::CoreError("Material with name {} doesn't exist in the registry!", name);
		}
		return mat;
	}
}