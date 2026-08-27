#pragma once

#include "Texture.hpp"
#include "Core/Buffer.hpp"

#include <filesystem>
#include <array>

namespace Kerberos {

struct FaceData
{
	TextureSpecification Specification;
	Buffer Buffer;
};
struct CubemapData
{
	std::string Name;
	std::array<FaceData, 6> Faces;
	bool IsSRGB = false;
};

class TextureCube : public Texture
{
public:
	// TODO: Only needed for manually setting up the fields, should be deleted later
	TextureCube() = default;

	explicit TextureCube(const CubemapData& data);
	explicit TextureCube(const std::filesystem::path& filepath);

	~TextureCube() override;

	AssetType GetType() override { return AssetType::Texture2D; }

	static Ref<TextureCube> FromData(const CubemapData& data);
	static Ref<TextureCube> FromFile(const std::filesystem::path& filepath);
};

}