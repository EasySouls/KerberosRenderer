#include "kbrpch.hpp"
#include "MaterialRegistry.hpp"

#include "VulkanContext.hpp"
#include "Logging/Log.hpp"

#include <ranges>
#include <format>

namespace Kerberos 
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

	void MaterialRegistry::SetupDescriptorSets(const vk::raii::DescriptorSetLayout& setLayout)
	{
		RecreateDescriptorPoolIfNeeded();

		const std::array<vk::DescriptorSetLayout, 1> setLayouts = {
			setLayout
		};

		const vk::DescriptorSetAllocateInfo allocInfo{
			.descriptorPool = m_TextureDescriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(setLayouts.size()),
			.pSetLayouts = setLayouts.data()
		};

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		for (const auto& [name, material]: m_Materials)
		{
			std::vector<vk::raii::DescriptorSet> descriptorSets = device.allocateDescriptorSets(allocInfo);
			material->DescriptorSet = std::move(descriptorSets[0]);
			//material->DescriptorSet = device.allocateDescriptorSets(allocInfo)[0];
			context.SetObjectDebugName(material->DescriptorSet, std::format("{} Descriptor Set", name));

			// TODO: This should not be the place where base textures are substituted
			std::vector< vk::WriteDescriptorSet> descriptorWrites{};
			if (material->AlbedoTexture != nullptr)
			{
				descriptorWrites.push_back(vk::WriteDescriptorSet{
					.dstSet = material->DescriptorSet,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &material->AlbedoTexture->GetDescriptorInfo()
				});
			}
			else
			{
				descriptorWrites.push_back(vk::WriteDescriptorSet{
					.dstSet = material->DescriptorSet,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &m_AlbedoPlaceholder.GetDescriptorInfo()
				});
			}

			if (material->NormalTexture != nullptr)
			{
				descriptorWrites.push_back(vk::WriteDescriptorSet{
					.dstSet = material->DescriptorSet,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &material->NormalTexture->GetDescriptorInfo()
				});
			}
			else 
			{
				descriptorWrites.push_back(vk::WriteDescriptorSet{
					.dstSet = material->DescriptorSet,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &m_NormalPlaceholder.GetDescriptorInfo()
				});
			}

			device.updateDescriptorSets(descriptorWrites, {});
		}
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

	void MaterialRegistry::RecreateDescriptorPoolIfNeeded() 
	{
		const uint32_t sizeNeeded = static_cast<uint32_t>(m_Materials.size());
		if (m_PoolSize > sizeNeeded) {
			return;
		}

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = sizeNeeded * m_TexturePerMaterial,
			}
		};

		const vk::DescriptorPoolCreateInfo poolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = static_cast<uint32_t>(m_Materials.size()),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};

		m_TextureDescriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
		context.SetObjectDebugName(m_TextureDescriptorPool, "Material Registry Descriptor Pool");

		m_PoolSize = sizeNeeded;

		// TODO: Do not do this here

		std::array<uint8_t, 4> albedoBuffer = { 1, 1, 1, 1 };
		/*TextureSpecification albedoSpec{};
		albedoSpec.Width = 1;
		albedoSpec.Height = 1;
		albedoSpec.Format = vk::Format::eR8G8B8A8Unorm;*/
		m_AlbedoPlaceholder.FromBuffer(albedoBuffer.data(), sizeof(uint8_t) * albedoBuffer.size(), vk::Format::eR8G8B8A8Unorm, 1, 1);

		std::array<uint8_t, 4> normalBuffer = { 1, 1, 1, 1 };
		m_NormalPlaceholder.FromBuffer(normalBuffer.data(), sizeof(uint8_t) * normalBuffer.size(), vk::Format::eR8G8B8A8Unorm, 1, 1);
	}
}
