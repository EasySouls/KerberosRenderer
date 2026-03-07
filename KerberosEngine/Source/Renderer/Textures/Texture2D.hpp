#pragma once

#include "Texture.hpp"
#include "Core/Buffer.hpp"

#include <filesystem>


namespace Kerberos
{
	class Texture2D : public Texture
	{
	public:
		Texture2D(const TextureSpecification& spec, const Buffer& buffer);
		explicit Texture2D(const std::filesystem::path& filepath);

		~Texture2D() override;

		AssetType GetType() override { return AssetType::Texture2D; }
	};
}
