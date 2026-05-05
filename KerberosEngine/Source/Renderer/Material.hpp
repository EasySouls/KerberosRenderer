#pragma once

#include "Vulkan.hpp"
#include "Assets/Asset.hpp"
#include "Textures/Texture2D.hpp"

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
			glm::vec3 EmissiveFactor{ 0.0f };
			float RoughnessFactor{ 1.0f };
			float MetallicFactor{ 0.0f };
		};
		UniformBlock Params{};

		std::string Name;

		Ref<Texture2D> AlbedoTexture = nullptr;
		Ref<Texture2D> NormalTexture = nullptr;
		Ref<Texture2D> RoughnessTexture = nullptr;
		Ref<Texture2D> MetallicTexture = nullptr;
		Ref<Texture2D> AOTexture = nullptr;
		Ref<Texture2D> EmissiveTexture = nullptr;

		std::vector<vk::raii::DescriptorSet> DescriptorSets;

		bool IsTransparent() const 
		{
			return Params.AlbedoFactor.a < 1.0f;
		}

		Material() = default;

		Material(std::string n, const glm::vec4 c, const float r, const float m)
			: Name(std::move(n))
		{
			Params.RoughnessFactor = r;
			Params.MetallicFactor = m;
			Params.AlbedoFactor = c;
			Params.EmissiveFactor = glm::vec3(0.0f);
		}

		Material(std::string name, const glm::vec4 c, const float r, const float m,
				 const std::shared_ptr<Texture2D>& albedoTex, const std::shared_ptr<Texture2D>& normalTex)
			: Name(std::move(name)), AlbedoTexture(albedoTex), NormalTexture(normalTex)
		{
			Params.RoughnessFactor = r;
			Params.MetallicFactor = m;
			Params.AlbedoFactor = c;
			Params.EmissiveFactor = glm::vec3(0.0f);
		}

		Material(const Material& other)
			: Params(other.Params)
			, Name(other.Name)
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
				AlbedoTexture = other.AlbedoTexture;
				NormalTexture = other.NormalTexture;
				MetallicTexture = other.MetallicTexture;
				RoughnessTexture = other.RoughnessTexture;
				AOTexture = other.AOTexture;
				EmissiveTexture = other.EmissiveTexture;
				// DescriptorSets intentionally not copied
			}
			return *this;
		}

		bool operator==(const Material& other) const
		{
			return Params.AlbedoFactor == other.Params.AlbedoFactor &&
				   Params.RoughnessFactor == other.Params.RoughnessFactor &&
				   Params.MetallicFactor == other.Params.MetallicFactor &&
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
