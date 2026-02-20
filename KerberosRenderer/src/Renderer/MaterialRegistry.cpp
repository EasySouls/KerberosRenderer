#include "pch.hpp"
#include "MaterialRegistry.hpp"

#include "Logging/Log.hpp"

namespace kbr 
{
	void MaterialRegistry::Add(const std::string& name, const Material& mat) 
	{
		if (m_Materials.contains(name)) {
			KBR_CORE_ERROR("Material with name {} already exists!", name);
		}

		m_Materials[name] = std::make_shared<Material>(mat);
	}

	void MaterialRegistry::Add(const std::string& name, const Ref<Material>& mat)
	{
		if (m_Materials.contains(name)) {
			KBR_CORE_ERROR("Material with name {} already exists!", name);
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

	const Ref<Material>& MaterialRegistry::Get(const std::string& name) const 
	{
		const auto& mat = m_Materials.at(name);
		if (mat == nullptr) {
			KBR_CORE_ERROR("Material with name {} doesn't exist in the registry!", name);
		}
		return mat;
	}

	Ref<Material>& MaterialRegistry::Get(const std::string& name) 
	{
		auto& mat = m_Materials[name];
		if (mat == nullptr) {
			KBR_CORE_ERROR("Material with name {} doesn't exist in the registry!", name);
		}
		return mat;
	}
}
