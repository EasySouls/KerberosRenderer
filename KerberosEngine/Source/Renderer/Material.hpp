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
			glm::vec3 albedo;
			float roughness;
			float metallic;
		};
		UniformBlock Params{};

		std::string name;

		Ref<Texture2D> AlbedoTexture = nullptr;
		Ref<Texture2D> NormalTexture = nullptr;
		Ref<Texture2D> RoughnessTexture = nullptr;
		Ref<Texture2D> MetallicTexture = nullptr;
		Ref<Texture2D> AOTexture = nullptr;

		std::vector<vk::raii::DescriptorSet> DescriptorSets;

		bool IsTransparent() const 
		{
			//return Params.albedo.a < 1.0f;
			return false;
		}

		Material() = default;

		Material(std::string n, const glm::vec3 c, const float r, const float m)
			: name(std::move(n))
		{
			Params.roughness = r;
			Params.metallic = m;
			Params.albedo = c;
		}

		Material(std::string name, const glm::vec3 c, const float r, const float m,
				 const std::shared_ptr<Texture2D>& albedoTex, const std::shared_ptr<Texture2D>& normalTex)
			: name(std::move(name)), AlbedoTexture(albedoTex), NormalTexture(normalTex)
		{
			Params.roughness = r;
			Params.metallic = m;
			Params.albedo = c;
		}

		Material(const Material& other)
			: Params(other.Params)
			, name(other.name)
			, AlbedoTexture(other.AlbedoTexture)
			, NormalTexture(other.NormalTexture)
			, RoughnessTexture(other.RoughnessTexture)
			, MetallicTexture(other.MetallicTexture)
			, AOTexture(other.AOTexture)
		{
		}

		Material& operator=(const Material& other)
		{
			if (this != &other)
			{
				Params = other.Params;
				name = other.name;
				AlbedoTexture = other.AlbedoTexture;
				NormalTexture = other.NormalTexture;
				MetallicTexture = other.MetallicTexture;
				RoughnessTexture = other.RoughnessTexture;
				AOTexture = other.AOTexture;
				// DescriptorSets intentionally not copied
			}
			return *this;
		}

		bool operator==(const Material& other) const
		{
			return Params.albedo == other.Params.albedo &&
				   Params.roughness == other.Params.roughness &&
				   Params.metallic == other.Params.metallic &&
				   AlbedoTexture == other.AlbedoTexture &&
				   NormalTexture == other.NormalTexture &&
				   RoughnessTexture == other.RoughnessTexture &&
				   MetallicTexture == other.MetallicTexture &&
				   AOTexture == other.AOTexture;
		}

		AssetType GetType() override { return AssetType::Material; }
	};
}
