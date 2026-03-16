#pragma once

#include "Core/Core.hpp"
#include "Textures/Texture2D.hpp"
#include "Vulkan.hpp"

#include <unordered_map>

namespace Kerberos
{
	class ImGuiDescriptorSetManager final
	{
	public:
		ImGuiDescriptorSetManager() = default;
		~ImGuiDescriptorSetManager();

		ImGuiDescriptorSetManager(const ImGuiDescriptorSetManager& other) = delete;
		ImGuiDescriptorSetManager(ImGuiDescriptorSetManager&& other) noexcept = delete;
		ImGuiDescriptorSetManager& operator=(const ImGuiDescriptorSetManager& other) = delete;
		ImGuiDescriptorSetManager& operator=(ImGuiDescriptorSetManager&& other) noexcept = delete;

		void Clear();

		vk::DescriptorSet GetDescriptorSetForTexture(const Ref<Texture2D>& texture);

	private:
		std::unordered_map<Ref<Texture2D>, vk::DescriptorSet> m_TextureDescriptorSets;
	};
}