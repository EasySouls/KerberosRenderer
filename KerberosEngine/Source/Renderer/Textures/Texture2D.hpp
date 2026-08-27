#pragma once

#include "Texture.hpp"
#include "Core/Buffer.hpp"

#include <filesystem>

namespace Kerberos {

class Texture2D : public Texture
{
public:
	// TODO: Only needed for manually setting up the fields, should be deleted later
	Texture2D() = default;

	Texture2D(const TextureSpecification& spec, const Buffer& buffer);
	explicit Texture2D(const std::filesystem::path& filepath);

	~Texture2D() override;

	AssetType GetType() override { return AssetType::Texture2D; }

	static Ref<Texture2D> FromBuffer(const TextureSpecification& spec, const Buffer& buffer);
	static Ref<Texture2D> FromFile(const std::filesystem::path& filepath);
};

}
