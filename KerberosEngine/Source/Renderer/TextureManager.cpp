#include "kbrpch.hpp"
#include "TextureManager.hpp"

#include "VulkanContext.hpp"
#include "Assets/AssetManager.hpp"

namespace Kerberos
{
	constexpr uint32_t globalTextureArrayBindingSlot = 0;
	constexpr uint32_t globalSamplerArrayBindingSlot = 1;

	TextureManager::TextureManager()
	{
		m_Samplers = std::array<vk::raii::Sampler, static_cast<uint32_t>(DefaultSampler::Count)>{nullptr, nullptr, nullptr, nullptr, nullptr};
	}

	TextureManager::~TextureManager()
	{
		Shutdown();
	}

	void TextureManager::Initialize()
	{
		constexpr TextureSpecification whiteSpec{
			.Width = 1,
			.Height = 1,
			.Format = ImageFormat::RGBA8,
		};
		m_WhiteTexture = Texture2D::FromBuffer(whiteSpec, Buffer{ std::vector<uint8_t>{ 255, 255, 255, 255 } });

		constexpr TextureSpecification blackSpec{
			.Width = 1,
			.Height = 1,
			.Format = ImageFormat::RGBA8,
		};
		m_BlackTexture = Texture2D::FromBuffer(blackSpec, Buffer{ std::vector<uint8_t>{ 0, 0, 0, 255 } });

		constexpr TextureSpecification defaultNormalSpec{
			.Width = 1,
			.Height = 1,
			.Format = ImageFormat::RGBA8,
		};
		m_DefaultNormalTexture = Texture2D::FromBuffer(defaultNormalSpec, Buffer{ std::vector<uint8_t>{ 128, 128, 255, 255 } });

		constexpr TextureSpecification defaultRoughnessSpec{
			.Width = 1,
			.Height = 1,
			.Format = ImageFormat::RGBA8,
		};
		m_DefaultRoughnessTexture = Texture2D::FromBuffer(defaultRoughnessSpec, Buffer{ std::vector<uint8_t>{ 255, 255, 255, 255 } });

		constexpr TextureSpecification defaultMetallicSpec{
			.Width = 1,
			.Height = 1,
			.Format = ImageFormat::RGBA8,
		};
		m_DefaultMetallicTexture = Texture2D::FromBuffer(defaultMetallicSpec, Buffer{ std::vector<uint8_t>{ 255, 255, 255, 255 } });

		constexpr TextureSpecification defaultAOSpec{
			.Width = 1,
			.Height = 1,
			.Format = ImageFormat::RGBA8,
		};
		m_DefaultAOTexture = Texture2D::FromBuffer(defaultAOSpec, Buffer{ std::vector<uint8_t>{ 255, 255, 255, 255 } });

		constexpr TextureSpecification defaultEmissiveSpec{
			.Width = 1,
			.Height = 1,
			.Format = ImageFormat::RGBA8,
		};
		m_DefaultEmissiveTexture = Texture2D::FromBuffer(defaultEmissiveSpec, Buffer{ std::vector<uint8_t>{ 255, 255, 255, 255 } });

		constexpr uint32_t maxSlotCount = 1024;
		m_FreeSlots.reserve(maxSlotCount);
		for (uint32_t i = 0; i < maxSlotCount; ++i)
			m_FreeSlots.push_back(i);

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		constexpr std::array bindings = {
			vk::DescriptorSetLayoutBinding{
				.binding = globalTextureArrayBindingSlot,
				.descriptorType = vk::DescriptorType::eSampledImage,
				.descriptorCount = maxSlotCount,
				.stageFlags = vk::ShaderStageFlagBits::eFragment
			},
			vk::DescriptorSetLayoutBinding{
				.binding = globalSamplerArrayBindingSlot,
				.descriptorType = vk::DescriptorType::eSampler,
				.descriptorCount = maxSlotCount,
				.stageFlags = vk::ShaderStageFlagBits::eFragment
			}
		};

		const std::vector<vk::DescriptorBindingFlags> textureBindingFlags(
			bindings.size(),
			vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind
		);

		const vk::DescriptorSetLayoutBindingFlagsCreateInfo textureLayoutBindingFlagsInfo{
			.bindingCount = static_cast<uint32_t>(textureBindingFlags.size()),
			.pBindingFlags = textureBindingFlags.data()
		};

		const vk::DescriptorSetLayoutCreateInfo layoutInfo{
			.pNext = &textureLayoutBindingFlagsInfo,
			.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		m_DescriptorSetLayout = vk::raii::DescriptorSetLayout{ device, layoutInfo };
		context.SetObjectDebugName(m_DescriptorSetLayout, "Texture Manager Descriptor Set Layout");

		constexpr std::array poolSizes = {
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eSampledImage,
				.descriptorCount = maxSlotCount,
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eSampler,
				.descriptorCount = maxSlotCount,
			}
		};

		const vk::DescriptorPoolCreateInfo poolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
			.maxSets = maxSlotCount,
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};

		m_DescriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
		context.SetObjectDebugName(m_DescriptorPool,"Texture Manager Descriptor Pool");

		const vk::DescriptorSetAllocateInfo allocInfo{
			.descriptorPool = *m_DescriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &(*m_DescriptorSetLayout)
		};

		auto descriptorSets = device.allocateDescriptorSets(allocInfo);
		m_GlobalDescriptorSet = std::move(descriptorSets[0]);
		context.SetObjectDebugName(m_GlobalDescriptorSet, "Texture Manager Global Descriptor Set");

		CreateAndUploadSamplers();
		UploadDefaultTextures();
	}

	void TextureManager::Shutdown() {
		throw std::logic_error("Not implemented");
	}

	uint32_t TextureManager::GetTextureIndex(AssetHandle handle)
	{
		if (handle == AssetHandle::Invalid())
		{
			KBR_CORE_ASSERT(false, "Texture handle is invalid {0}", handle);
			return GetWhiteTexture();
		}

		const auto it = m_AssetToSlotMap.find(handle);
		if (it != m_AssetToSlotMap.end())
			return it->second;

		const Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle);
		if (!texture)
		{
			KBR_CORE_ASSERT(false, "Texture not found for handle: {0}", handle);
			return GetWhiteTexture();
		}

		const vk::ImageView imageView = texture->GetImageView();

		const uint32_t slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, imageView);

		m_AssetToSlotMap[handle] = slot;

		return slot;
	}

	uint32_t TextureManager::GetTextureIndex(const Ref<Texture2D>& texture)
	{
		return GetTextureIndex(texture->GetHandle());
	}

	vk::DescriptorSetLayout TextureManager::GetGlobalDescriptorSetLayout() const 
	{
		KBR_CORE_ASSERT(m_DescriptorSetLayout != nullptr, "Global descriptor set layout is not initialized!");
		return m_DescriptorSetLayout;
	}

	vk::DescriptorSet TextureManager::GetGlobalDescriptorSet() const
	{
		KBR_CORE_ASSERT(m_GlobalDescriptorSet != nullptr, "Global descriptor set is not initialized!");
		return m_GlobalDescriptorSet;
	}

	uint32_t TextureManager::GetWhiteTexture()
	{
		KBR_CORE_ASSERT(m_WhiteTexture != nullptr, "White texture is not initialized!");
		return GetTextureIndex(m_WhiteTexture->GetHandle());
	}

	uint32_t TextureManager::GetBlackTexture() 
	{
		KBR_CORE_ASSERT(m_BlackTexture != nullptr, "Black texture is not initialized!");
		return GetTextureIndex(m_BlackTexture->GetHandle());
	}

	uint32_t TextureManager::GetDefaultNormalTexture()
	{
		KBR_CORE_ASSERT(m_DefaultNormalTexture != nullptr, "Default normal texture is not initialized!");
		return GetTextureIndex(m_DefaultNormalTexture->GetHandle());
	}

	uint32_t TextureManager::GetDefaultRoughnessTexture() 
	{
		KBR_CORE_ASSERT(m_DefaultRoughnessTexture != nullptr, "Default roughness texture is not initialized!");
		return GetTextureIndex(m_DefaultRoughnessTexture->GetHandle());
	}

	uint32_t TextureManager::GetDefaultMetallicTexture() 
	{
		KBR_CORE_ASSERT(m_DefaultMetallicTexture != nullptr, "Default metallic texture is not initialized!");
		return GetTextureIndex(m_DefaultMetallicTexture->GetHandle());
	}

	uint32_t TextureManager::GetDefaultAOTexture() 
	{
		KBR_CORE_ASSERT(m_DefaultAOTexture != nullptr, "Default AO texture is not initialized!");
		return GetTextureIndex(m_DefaultAOTexture->GetHandle());
	}

	uint32_t TextureManager::GetDefaultEmissiveTexture()
	{
		KBR_CORE_ASSERT(m_DefaultEmissiveTexture != nullptr, "Default emissive texture is not initialized!");
		return GetTextureIndex(m_DefaultEmissiveTexture->GetHandle());
	}

	uint32_t TextureManager::Pack(const uint32_t textureIndex, DefaultSampler sampler)
	{
		const uint32_t sampIdx = static_cast<uint32_t>(sampler);
		return (sampIdx << 24) | (textureIndex & 0x00FFFFFF);
	}

	uint32_t TextureManager::AllocateSlot()
	{
		if (m_FreeSlots.empty())
		{
			KBR_CORE_ERROR("No free texture slots available!");
			throw std::runtime_error("No free texture slots available!");
		}

		const uint32_t slot = m_FreeSlots.back();
		m_FreeSlots.pop_back();
		return slot;
	}

	void TextureManager::UpdateGlobalDescriptorSet(const uint32_t index, const vk::ImageView imageView) const
	{
		const vk::DescriptorImageInfo imageInfo{
			.sampler = nullptr,
			.imageView = imageView,
			.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
		};

		const vk::WriteDescriptorSet write{
			.dstSet = m_GlobalDescriptorSet,
			.dstBinding = globalTextureArrayBindingSlot,
			.dstArrayElement = index,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eSampledImage,
			.pImageInfo = &imageInfo
		};

		const auto& device = m_GlobalDescriptorSet.getDevice();
		device.updateDescriptorSets(write, nullptr);
	}

	void TextureManager::CreateAndUploadSamplers()
	{
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		const vk::SamplerCreateInfo linearWrapSamplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			.addressModeW = vk::SamplerAddressMode::eRepeat,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::True,
			.maxAnisotropy = context.GetMaxAnisotropy(),
			.compareEnable = vk::False,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0f,
			.maxLod = vk::LodClampNone,
			.borderColor = vk::BorderColor::eIntOpaqueBlack,
			.unnormalizedCoordinates = vk::False
		};
		m_Samplers[static_cast<uint32_t>(DefaultSampler::LinearWrap)] = vk::raii::Sampler{ device, linearWrapSamplerInfo };

		context.SetObjectDebugName(m_Samplers[static_cast<uint32_t>(DefaultSampler::LinearWrap)], "Linear Wrap Sampler");

		const vk::SamplerCreateInfo linearClampSamplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::False,
			.maxAnisotropy = context.GetMaxAnisotropy(),
			.compareEnable = vk::False,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = vk::BorderColor::eIntOpaqueBlack,
			.unnormalizedCoordinates = vk::False
		};
		m_Samplers[static_cast<uint32_t>(DefaultSampler::LinearClamp)] = vk::raii::Sampler{ device, linearClampSamplerInfo };

		context.SetObjectDebugName(m_Samplers[static_cast<uint32_t>(DefaultSampler::LinearClamp)], "Linear Clamp Sampler");

		const vk::SamplerCreateInfo pointWrapSamplerInfo{
			.magFilter = vk::Filter::eNearest,
			.minFilter = vk::Filter::eNearest,
			.mipmapMode = vk::SamplerMipmapMode::eNearest,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			.addressModeW = vk::SamplerAddressMode::eRepeat,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::False,
			.maxAnisotropy = context.GetMaxAnisotropy(),
			.compareEnable = vk::False,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = vk::BorderColor::eIntOpaqueBlack,
			.unnormalizedCoordinates = vk::False
		};
		m_Samplers[static_cast<uint32_t>(DefaultSampler::PointWrap)] = vk::raii::Sampler{ device, pointWrapSamplerInfo };

		context.SetObjectDebugName(m_Samplers[static_cast<uint32_t>(DefaultSampler::PointWrap)], "Point Wrap Sampler");

		const vk::SamplerCreateInfo pointClampSamplerInfo{
			.magFilter = vk::Filter::eNearest,
			.minFilter = vk::Filter::eNearest,
			.mipmapMode = vk::SamplerMipmapMode::eNearest,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::False,
			.maxAnisotropy = context.GetMaxAnisotropy(),
			.compareEnable = vk::False,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = vk::BorderColor::eIntOpaqueBlack,
			.unnormalizedCoordinates = vk::False
		};
		m_Samplers[static_cast<uint32_t>(DefaultSampler::PointClamp)] = vk::raii::Sampler{ device, pointClampSamplerInfo };

		context.SetObjectDebugName(m_Samplers[static_cast<uint32_t>(DefaultSampler::PointClamp)], "Point Clamp Sampler");

		const vk::SamplerCreateInfo anisoWrapSamplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			.addressModeW = vk::SamplerAddressMode::eRepeat,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::True,
			.maxAnisotropy = context.GetMaxAnisotropy(),
			.compareEnable = vk::False,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0f,
			.maxLod = vk::LodClampNone,
			.borderColor = vk::BorderColor::eIntOpaqueBlack,
			.unnormalizedCoordinates = vk::False
		};

		m_Samplers[static_cast<uint32_t>(DefaultSampler::AnisoWrap)] = vk::raii::Sampler{ device, anisoWrapSamplerInfo };

		context.SetObjectDebugName(m_Samplers[static_cast<uint32_t>(DefaultSampler::AnisoWrap)], "Aniso Wrap Sampler");

		std::vector<vk::WriteDescriptorSet> samplerWrites;
		samplerWrites.reserve(static_cast<uint32_t>(DefaultSampler::Count));
		for (uint32_t i = 0; i < static_cast<uint32_t>(DefaultSampler::Count); ++i)
		{
			const vk::DescriptorImageInfo samplerImageInfo{
				.sampler = *m_Samplers[i],
				.imageView = nullptr,
				.imageLayout = vk::ImageLayout::eUndefined
			};
			const vk::WriteDescriptorSet samplerWrite{
				.dstSet = m_GlobalDescriptorSet,
				.dstBinding = globalSamplerArrayBindingSlot,
				.dstArrayElement = i,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eSampler,
				.pImageInfo = &samplerImageInfo,
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr
			};
			samplerWrites.push_back(samplerWrite);
		}
		device.updateDescriptorSets(samplerWrites, nullptr);
	}

	void TextureManager::UploadDefaultTextures()
	{
		uint32_t slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, m_WhiteTexture->GetImageView());
		m_AssetToSlotMap[m_WhiteTexture->GetHandle()] = slot;

		slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, m_BlackTexture->GetImageView());
		m_AssetToSlotMap[m_BlackTexture->GetHandle()] = slot;

		slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, m_DefaultNormalTexture->GetImageView());
		m_AssetToSlotMap[m_DefaultNormalTexture->GetHandle()] = slot;

		slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, m_DefaultRoughnessTexture->GetImageView());
		m_AssetToSlotMap[m_DefaultRoughnessTexture->GetHandle()] = slot;

		slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, m_DefaultMetallicTexture->GetImageView());
		m_AssetToSlotMap[m_DefaultMetallicTexture->GetHandle()] = slot;

		slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, m_DefaultAOTexture->GetImageView());
		m_AssetToSlotMap[m_DefaultAOTexture->GetHandle()] = slot;

		slot = AllocateSlot();
		UpdateGlobalDescriptorSet(slot, m_DefaultEmissiveTexture->GetImageView());
		m_AssetToSlotMap[m_DefaultEmissiveTexture->GetHandle()] = slot;
	}
}
