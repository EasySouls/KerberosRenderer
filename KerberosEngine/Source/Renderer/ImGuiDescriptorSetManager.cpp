#include "kbrpch.hpp"
#include "ImGuiDescriptorSetManager.hpp"

#include <ranges>

#include "VulkanContext.hpp"

namespace Kerberos
{
	ImGuiDescriptorSetManager::~ImGuiDescriptorSetManager()
	{
		Clear();
	}

	void ImGuiDescriptorSetManager::Clear()
	{
		for (const auto& descriptorSet : m_TextureDescriptorSets | std::views::values)
		{
			VulkanContext::DestroyImGuiDescriptorSet(descriptorSet);
		}
		m_TextureDescriptorSets.clear();
	}

	vk::DescriptorSet ImGuiDescriptorSetManager::GetDescriptorSetForTexture(const Ref<Texture2D>& texture)
	{
		const auto it = m_TextureDescriptorSets.find(texture);
		if (it != m_TextureDescriptorSets.end())
			return it->second;

		const vk::DescriptorSet descriptorSet = VulkanContext::GenerateImGuiDescriptorSet(texture->GetSampler(), texture->GetImageView());
		m_TextureDescriptorSets[texture] = descriptorSet;
		return descriptorSet;
	}
}
