#include "kbrpch.hpp"
#include "MaterialRegistry.hpp"

#include "VulkanContext.hpp"
#include "Logging/Log.hpp"

#include <ranges>
#include <format>

namespace Kerberos 
{
	MaterialRegistry::~MaterialRegistry()
	{
		VulkanContext::Get().WaitIdle();
	}

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

	void MaterialRegistry::SetupDescriptorSets(const vk::DescriptorSetLayout& setLayout)
	{
		m_SetLayout = setLayout;

		InitPlaceholdersIfNeeded();

		for (const auto& material : m_Materials | std::views::values)
		{
			AllocateDescriptorSets(material);
		}
	}

	void MaterialRegistry::UpdateDescriptorSetsForMaterials(const std::set<Ref<Material>>& set)
	{
		if (set.empty()) return;

		// Wait for the device to be idle before updating descriptor sets that might be in use by command buffers
		//VulkanContext::Get().WaitIdle();

		for (const auto& material : set)
		{
			AllocateDescriptorSets(material);
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

	void MaterialRegistry::AllocateDescriptorSets(const Ref<Material>& material) 
	{
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();
		constexpr uint32_t maxFramesInFlight = VulkanContext::MaxFramesInFlight;

		const bool hasDescriptorSets = !material->DescriptorSets.empty();

		if (!hasDescriptorSets)
		{
			if (m_DescriptorPools.empty() || m_SetsAllocatedInCurrentPool + maxFramesInFlight > maxSetsPerPool) {
				std::vector<vk::DescriptorPoolSize> poolSizes = {
					vk::DescriptorPoolSize{
						.type = vk::DescriptorType::eCombinedImageSampler,
						.descriptorCount = maxSetsPerPool * m_TexturePerMaterial,
					}
				};

				const vk::DescriptorPoolCreateInfo poolInfo{
					.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
					.maxSets = maxSetsPerPool,
					.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
					.pPoolSizes = poolSizes.data()
				};

				m_DescriptorPools.emplace_back(device, poolInfo);
				context.SetObjectDebugName(m_DescriptorPools.back(), std::format("Material Registry Descriptor Pool {}", m_DescriptorPools.size()));
				m_SetsAllocatedInCurrentPool = 0;
			}

			std::vector<vk::DescriptorSetLayout> setLayouts(maxFramesInFlight, m_SetLayout);
			const vk::DescriptorSetAllocateInfo allocInfo{
				.descriptorPool = m_DescriptorPools.back(),
				.descriptorSetCount = maxFramesInFlight,
				.pSetLayouts = setLayouts.data()
			};

			material->DescriptorSets = device.allocateDescriptorSets(allocInfo);
			m_SetsAllocatedInCurrentPool += maxFramesInFlight;
		}

		for (uint32_t i = 0; i < maxFramesInFlight; ++i)
		{
			if (!hasDescriptorSets)
			{
				context.SetObjectDebugName(material->DescriptorSets[i], std::format("{} Descriptor Set Frame {}", material->Name, i));
			}

			std::vector<vk::WriteDescriptorSet> descriptorWrites;
			descriptorWrites.reserve(m_TexturePerMaterial);

			descriptorWrites.push_back(vk::WriteDescriptorSet{
				.dstSet = material->DescriptorSets[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = material->AlbedoTexture ? &material->AlbedoTexture->GetDescriptorInfo() : &m_AlbedoPlaceholder->GetDescriptorInfo()
			});

			descriptorWrites.push_back(vk::WriteDescriptorSet{
				.dstSet = material->DescriptorSets[i],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = material->NormalTexture ? &material->NormalTexture->GetDescriptorInfo() : &m_NormalPlaceholder->GetDescriptorInfo()
			});

			descriptorWrites.push_back(vk::WriteDescriptorSet{
				.dstSet = material->DescriptorSets[i],
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = material->RoughnessTexture ? &material->RoughnessTexture->GetDescriptorInfo() : &m_RoughnessPlaceholder->GetDescriptorInfo()
			});

			descriptorWrites.push_back(vk::WriteDescriptorSet{
				.dstSet = material->DescriptorSets[i],
				.dstBinding = 3,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = material->MetallicTexture ? &material->MetallicTexture->GetDescriptorInfo() : &m_MetallicPlaceholder->GetDescriptorInfo()
			});

			descriptorWrites.push_back(vk::WriteDescriptorSet{
				.dstSet = material->DescriptorSets[i],
				.dstBinding = 4,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = material->AOTexture ? &material->AOTexture->GetDescriptorInfo() : &m_AOPlaceholder->GetDescriptorInfo()
			});

			device.updateDescriptorSets(descriptorWrites, {});
		}
	}

	void MaterialRegistry::InitPlaceholdersIfNeeded() 
	{
		if (m_AlbedoPlaceholder != nullptr) return;

		// White placeholder texture for albedo
		constexpr std::array<uint8_t, 4> albedoBuffer = { 255, 255, 255, 255 };
		TextureSpecification albedoSpec{};
		albedoSpec.Width = 1;
		albedoSpec.Height = 1;
		albedoSpec.Format = ImageFormat::RGBA8;
		const Buffer albedoBufferStruct{ sizeof(uint8_t) * albedoBuffer.size() };
		std::memcpy(albedoBufferStruct.Data, albedoBuffer.data(), albedoBufferStruct.Size);
		m_AlbedoPlaceholder = Texture2D::FromBuffer(albedoSpec, albedoBufferStruct);

		// Flat normal placeholder — (128, 128, 255) decodes to tangent-space (0, 0, 1)
		constexpr std::array<uint8_t, 4> normalBuffer = { 128, 128, 255, 255 };
		TextureSpecification normalSpec{};
		normalSpec.Width = 1;
		normalSpec.Height = 1;
		normalSpec.Format = ImageFormat::RGBA8; // UNORM
		const Buffer normalBufferStruct{ sizeof(uint8_t) * normalBuffer.size() };
		std::memcpy(normalBufferStruct.Data, normalBuffer.data(), normalBufferStruct.Size);
		m_NormalPlaceholder = Texture2D::FromBuffer(normalSpec, normalBufferStruct);

		constexpr std::array<uint8_t, 4> roughnessBuffer = { 255, 255, 255, 255 };
		TextureSpecification roughnessSpec{};
		roughnessSpec.Width = 1;
		roughnessSpec.Height = 1;
		roughnessSpec.Format = ImageFormat::RGBA8; // UNORM
		const Buffer roughnessBufferStruct{ sizeof(uint8_t) * roughnessBuffer.size() };
		std::memcpy(roughnessBufferStruct.Data, roughnessBuffer.data(), roughnessBufferStruct.Size);
		m_RoughnessPlaceholder = Texture2D::FromBuffer(roughnessSpec, roughnessBufferStruct);

		constexpr std::array<uint8_t, 4> metallicBuffer = { 0, 0, 0, 255 };
		TextureSpecification metallicSpec{};
		metallicSpec.Width = 1;
		metallicSpec.Height = 1;
		metallicSpec.Format = ImageFormat::RGBA8; // UNORM
		const Buffer metallicBufferStruct{ sizeof(uint8_t) * metallicBuffer.size() };
		std::memcpy(metallicBufferStruct.Data, metallicBuffer.data(), metallicBufferStruct.Size);
		m_MetallicPlaceholder = Texture2D::FromBuffer(metallicSpec, metallicBufferStruct);

		constexpr std::array<uint8_t, 4> aoBuffer = { 255, 255, 255, 255 };
		TextureSpecification aoSpec{};
		aoSpec.Width = 1;
		aoSpec.Height = 1;
		aoSpec.Format = ImageFormat::RGBA8; // UNORM
		const Buffer aoBufferStruct{ sizeof(uint8_t) * aoBuffer.size() };
		std::memcpy(aoBufferStruct.Data, aoBuffer.data(), aoBufferStruct.Size);
		m_AOPlaceholder = Texture2D::FromBuffer(aoSpec, aoBufferStruct);
	}
}