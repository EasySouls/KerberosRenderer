#pragma once

#include "Vulkan.hpp"
#include "Assets/Asset.hpp"
#include "Textures/Texture2D.hpp"
#include "TextureManager.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <memory>

namespace Kerberos  {

/// Encoding of the authored color factors stored on a Material. 
/// The renderer converts sRGB-authored factors to linear in the per-object uniform buffer.
enum class ColorEncoding : std::uint8_t
{
	Srgb,
	Linear
};

/// `Opaque` ignores the base-color alpha, `Mask` uses a
/// hard cutoff, and `Blend` renders through the weighted-blended OIT pass.
enum class AlphaMode : std::uint8_t
{
	Opaque = 0,
	Mask = 1,
	Blend = 2
};

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
    
	ColorEncoding ColorEncoding = ColorEncoding::Srgb;

	AlphaMode AlphaMode = AlphaMode::Opaque;
	float AlphaCutoff = 0.5f;

	Ref<Texture2D> AlbedoTexture = nullptr;
	Ref<Texture2D> NormalTexture = nullptr;
	Ref<Texture2D> RoughnessTexture = nullptr;
	Ref<Texture2D> MetallicTexture = nullptr;
	Ref<Texture2D> AOTexture = nullptr;
	Ref<Texture2D> EmissiveTexture = nullptr;

	/// True when the material belongs in the weighted-blended OIT pass.
	/// Masked materials stay in the opaque pass and are alpha-tested in the
	/// depth, shadow, and PBR shaders.
	bool IsTransparent() const 
	{
		return AlphaMode == AlphaMode::Blend;
	}

	bool IsMasked() const
	{
		return AlphaMode == AlphaMode::Mask;
	}

	void ResolveIndices(TextureManager& textureManager);

    Material() = default;

	Material(std::string n, const glm::vec4 c, const float r, const float m);

	Material(std::string name, const glm::vec4 c, const float r, const float m,
             const Ref<Texture2D>& albedoTex, const Ref<Texture2D>& normalTex);

	Material(const Material& other);

	Material& operator=(const Material& other);

    bool operator==(const Material& other) const;

    static constexpr AssetType Type = AssetType::Material;
    AssetType GetType() override
    {
        return Type;
    }
};

}
