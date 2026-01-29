#pragma once

#include "Textures.hpp"
#include "Vulkan.hpp"

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

		vk::DescriptorSet DescriptorSet = nullptr;

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
	};
}
