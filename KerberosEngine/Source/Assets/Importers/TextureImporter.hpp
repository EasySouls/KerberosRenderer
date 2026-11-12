#pragma once

#include "Assets/AssetMetadata.hpp"
#include "Core/Buffer.hpp"

namespace Kerberos
{
	class Texture2D;
	struct TextureSpecification;

	class TextureImporter
	{
	public:
		static Ref<Texture2D> ImportTexture(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<Texture2D> ImportTexture(const std::filesystem::path& filepath, bool srgb = true);

		/**
		 * Loads texture data from a file into a Buffer and returns the corresponding TextureSpecification.
		 * @param filepath The path to the texture file.
		 * @param flip Whether to flip the image vertically.
		 * @param desiredChannels The number of channels to load (0 for automatic).
		 * @param srgb Whether to interpret the image as sRGB color data. 8-bit color
		 * data is stored in an sRGB format (hardware-decoded to linear on
		 * sampling); 16-bit color data is linearized on the CPU because Vulkan has
		 * no 16-bit UNORM+sRGB format. HDR sources always load as RGBA32F and
		 * ignore this flag. Single and dual channel images stay linear.
		 * @return A pair containing the TextureSpecification and the loaded Buffer.
		 */
		static std::pair<TextureSpecification, Buffer> LoadTextureData(const std::filesystem::path& filepath, bool flip, int desiredChannels = 0, bool srgb = true);
	};
}