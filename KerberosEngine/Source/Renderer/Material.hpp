#pragma once

#include "Vulkan.hpp"
#include "Assets/Asset.hpp"
#include "Textures/Texture2D.hpp"
#include "TextureManager.hpp"

#include <glm/vec3.hpp>

#include <string>
#include <memory>


namespace Kerberos 
{
	struct Material : public Asset
	{
		// Parameter block used as uniform buffer block
		struct UniformBlock
		{
			glm::vec4 AlbedoFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec3 Emissive{ 0.0f };
			float RoughnessFactor{ 1.0f };
			float MetallicFactor{ 0.0f };
			uint32_t AlbedoIndex = 0;
			uint32_t NormalIndex = 0;
			uint32_t RoughnessIndex = 0;
			uint32_t MetallicIndex = 0;
			uint32_t AOIndex = 0;
			uint32_t EmissiveIndex = 0;
		};
		UniformBlock Params{};

		std::string Name;

		glm::vec3 EmissiveColor{ 0.0f };
		float EmissiveIntensity{ 0.0f };

		Ref<Texture2D> AlbedoTexture = nullptr;
		Ref<Texture2D> NormalTexture = nullptr;
		Ref<Texture2D> RoughnessTexture = nullptr;
		Ref<Texture2D> MetallicTexture = nullptr;
		Ref<Texture2D> AOTexture = nullptr;
		Ref<Texture2D> EmissiveTexture = nullptr;

		bool IsTransparent() const 
		{
			return Params.AlbedoFactor.a < 1.0f;
		}

		void ResolveIndices(TextureManager& textureManager)
		{
			const uint32_t albedoTex = AlbedoTexture ? textureManager.GetTextureIndex(AlbedoTexture) : textureManager.GetWhiteTexture();
			Params.AlbedoIndex = TextureManager::Pack(albedoTex, DefaultSampler::AnisoWrap);

			const uint32_t normalTex = NormalTexture ? textureManager.GetTextureIndex(NormalTexture) : textureManager.GetDefaultNormalTexture();
			Params.NormalIndex = TextureManager::Pack(normalTex, DefaultSampler::LinearWrap);

			const uint32_t roughnessTex = RoughnessTexture ? textureManager.GetTextureIndex(RoughnessTexture) : textureManager.GetDefaultRoughnessTexture();
			Params.RoughnessIndex = TextureManager::Pack(roughnessTex, DefaultSampler::LinearWrap);

			const uint32_t metallicTex = MetallicTexture ? textureManager.GetTextureIndex(MetallicTexture) : textureManager.GetDefaultMetallicTexture();
			Params.MetallicIndex = TextureManager::Pack(metallicTex, DefaultSampler::LinearWrap);

			const uint32_t aoTex = AOTexture ? textureManager.GetTextureIndex(AOTexture) : textureManager.GetDefaultAOTexture();
			Params.AOIndex = TextureManager::Pack(aoTex, DefaultSampler::LinearWrap);

			const uint32_t emissiveTex = EmissiveTexture ? textureManager.GetTextureIndex(EmissiveTexture) : textureManager.GetDefaultEmissiveTexture();
			Params.EmissiveIndex = TextureManager::Pack(emissiveTex, DefaultSampler::LinearWrap);

			Params.Emissive = EmissiveColor * EmissiveIntensity;
		}

		Material() = default;

		Material(std::string n, const glm::vec4 c, const float r, const float m)
			: Name(std::move(n))
		{
			Params.RoughnessFactor = r;
			Params.MetallicFactor = m;
			Params.AlbedoFactor = c;
			Params.Emissive = glm::vec3(0.0f);
		}

		Material(std::string name, const glm::vec4 c, const float r, const float m,
				 const std::shared_ptr<Texture2D>& albedoTex, const std::shared_ptr<Texture2D>& normalTex)
			: Name(std::move(name)), AlbedoTexture(albedoTex), NormalTexture(normalTex)
		{
			Params.RoughnessFactor = r;
			Params.MetallicFactor = m;
			Params.AlbedoFactor = c;
			Params.Emissive = glm::vec3(0.0f);
		}

		Material(const Material& other)
			: Params(other.Params)
			, Name(other.Name)
			, EmissiveColor(other.EmissiveColor)
			, EmissiveIntensity(other.EmissiveIntensity)
			, AlbedoTexture(other.AlbedoTexture)
			, NormalTexture(other.NormalTexture)
			, RoughnessTexture(other.RoughnessTexture)
			, MetallicTexture(other.MetallicTexture)
			, AOTexture(other.AOTexture)
			, EmissiveTexture(other.EmissiveTexture)
		{
		}

		Material& operator=(const Material& other)
		{
			if (this != &other)
			{
				Params = other.Params;
				Name = other.Name;
				EmissiveColor = other.EmissiveColor;
				EmissiveIntensity = other.EmissiveIntensity;
				AlbedoTexture = other.AlbedoTexture;
				NormalTexture = other.NormalTexture;
				MetallicTexture = other.MetallicTexture;
				RoughnessTexture = other.RoughnessTexture;
				AOTexture = other.AOTexture;
				EmissiveTexture = other.EmissiveTexture;
			}
			return *this;
		}

		bool operator==(const Material& other) const
		{
			return Params.AlbedoFactor == other.Params.AlbedoFactor &&
				   Params.Emissive == other.Params.Emissive &&
				   Params.RoughnessFactor == other.Params.RoughnessFactor &&
				   Params.MetallicFactor == other.Params.MetallicFactor &&
				   EmissiveColor == other.EmissiveColor &&
				   EmissiveIntensity == other.EmissiveIntensity &&
				   AlbedoTexture == other.AlbedoTexture &&
				   NormalTexture == other.NormalTexture &&
				   RoughnessTexture == other.RoughnessTexture &&
				   MetallicTexture == other.MetallicTexture &&
				   AOTexture == other.AOTexture &&
				   EmissiveTexture == other.EmissiveTexture;
		}

		AssetType GetType() override { return AssetType::Material; }
	};
}
