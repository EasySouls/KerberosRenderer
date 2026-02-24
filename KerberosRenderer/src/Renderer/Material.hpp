#pragma once

#include "Vulkan.hpp"
#include "Textures.hpp"

#include <glm/vec3.hpp>

#include <string>
#include <memory>

namespace kbr 
{
	struct Material
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
		std::shared_ptr<Texture2D> AlbedoTexture = nullptr;
		std::shared_ptr<Texture2D> NormalTexture = nullptr;

		vk::raii::DescriptorSet DescriptorSet{ nullptr };

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
				// DescriptorSet intentionally not copied
			}
			return *this;
		}
	};
}
