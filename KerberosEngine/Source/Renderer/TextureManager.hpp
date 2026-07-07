#pragma once

#include "Core/Core.hpp"
#include "Textures/Texture2D.hpp"
#include "Assets/Asset.hpp"

#include <unordered_map>
#include <vector>

namespace Kerberos
{
	enum class DefaultSampler : uint32_t
	{
		LinearWrap = 0,
		LinearClamp = 1,
		PointWrap = 2,
		PointClamp = 3,
		AnisoWrap = 4,
		Count = 5
	};

	class TextureManager
	{
	public:
		TextureManager();
		~TextureManager();

		void Initialize();
		void Shutdown();

		uint32_t GetTextureIndex(AssetHandle handle);
		uint32_t GetTextureIndex(const Ref<Texture2D>& texture);

		vk::DescriptorSetLayout GetGlobalDescriptorSetLayout() const;
		vk::DescriptorSet GetGlobalDescriptorSet() const;

		uint32_t GetWhiteTexture();
		uint32_t GetBlackTexture();
		uint32_t GetDefaultNormalTexture();
		uint32_t GetDefaultRoughnessTexture();
		uint32_t GetDefaultMetallicTexture();
		uint32_t GetDefaultAOTexture();
		uint32_t GetDefaultEmissiveTexture();

		static uint32_t Pack(uint32_t textureIndex, DefaultSampler sampler);

	private:
		uint32_t AllocateSlot();
		void UpdateGlobalDescriptorSet(uint32_t index, vk::ImageView imageView) const;

		void CreateAndUploadSamplers();
		void UploadDefaultTextures();

	private:
		std::unordered_map<AssetHandle, uint32_t> m_AssetToSlotMap{};
		std::vector<uint32_t> m_FreeSlots{};

		vk::raii::DescriptorSetLayout m_DescriptorSetLayout = nullptr;
		vk::raii::DescriptorPool m_DescriptorPool = nullptr;
		vk::raii::DescriptorSet m_GlobalDescriptorSet = nullptr;

		std::array<vk::raii::Sampler, static_cast<uint32_t>(DefaultSampler::Count)> m_Samplers{nullptr, nullptr, nullptr, nullptr, nullptr};

		Ref<Texture2D> m_WhiteTexture;
		Ref<Texture2D> m_BlackTexture;
		Ref<Texture2D> m_DefaultNormalTexture;
		Ref<Texture2D> m_DefaultRoughnessTexture;
		Ref<Texture2D> m_DefaultMetallicTexture;
		Ref<Texture2D> m_DefaultAOTexture;
		Ref<Texture2D> m_DefaultEmissiveTexture;
	};
}
