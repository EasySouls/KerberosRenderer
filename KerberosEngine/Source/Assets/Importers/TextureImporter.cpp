#include "kbrpch.hpp"
#include "TextureImporter.hpp"
#include "Renderer/Textures/Texture2D.hpp"

#include <stb_image.h>

#include <unordered_set>

namespace 
{
	const std::unordered_set<std::string> SupportedExtensions = { ".png", ".jpg", ".jpeg", ".ktx", ".ktx2" };

	bool IsExtensionSupported(const std::filesystem::path& filepath)
	{
		const auto extension = filepath.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
		return SupportedExtensions.contains(extension);
	}

	bool IsKTXFormat(const std::filesystem::path& filepath)
	{
		const auto extension = filepath.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
		return extension == ".ktx" || extension == ".ktx2";
	}
}

namespace Kerberos
{
	Ref<Texture2D> TextureImporter::ImportTexture(AssetHandle handle, const AssetMetadata& metadata)
	{
		return ImportTexture(metadata.Filepath);
	}

	Ref<Texture2D> TextureImporter::ImportTexture(const std::filesystem::path& filepath)
	{
		KBR_PROFILE_FUNCTION();

		const auto extension = filepath.extension();
		if (!IsExtensionSupported(filepath))
		{
			KBR_CORE_ERROR("TextureImporter::ImportTexture - unsupported texture file format: {}", extension.string());
			KBR_CORE_ASSERT(false, "TextureImporter::ImportTexture - unsupported texture file format: {}", extension.string());
			return nullptr;
		}

		if (IsKTXFormat(filepath))
		{
			const Ref<Texture2D> texture = CreateRef<Texture2D>(filepath);

			const std::string name = filepath.filename().string();
			//texture->SetDebugName(name);
		}
		else
		{
			const auto [spec, data] = LoadTextureData(filepath, true);

			auto texture = CreateRef<Texture2D>(spec, data);

			const std::string name = filepath.filename().string();
			//texture->SetDebugName(name);

			stbi_image_free(data.Data);

			return texture;
		}
	}

	std::pair<TextureSpecification, Buffer> TextureImporter::LoadTextureData(const std::filesystem::path& filepath, const bool flip, const int desiredChannels)
	{
		int width, height, channels;

		stbi_set_flip_vertically_on_load(flip);
		Buffer data;

		{
			KBR_PROFILE_SCOPE("TextureImporter::ImportTexture - stbi_load");
			data.Data = stbi_load(filepath.string().c_str(), &width, &height, &channels, desiredChannels);
		}

		if (data.Data == nullptr)
		{
			KBR_CORE_ERROR("TextureImporter::ImportTexture - failed to load texture from filepath: {}", filepath.string());
			return std::make_pair(TextureSpecification{}, Buffer{});
		}

		const int actualChannels = desiredChannels == 0 ? channels : desiredChannels;
		data.Size = static_cast<uint64_t>(width) * height * actualChannels;

		TextureSpecification spec;
		spec.Width = width;
		spec.Height = height;
		switch (actualChannels)
		{
			case 1:
				spec.Format = ImageFormat::R8;
				break;
			case 3:
				spec.Format = ImageFormat::RGB8;
				break;
			case 4:
				spec.Format = ImageFormat::RGBA8;
				break;
			default:
				KBR_CORE_ASSERT(false, "TextureImporter::ImportTexture - unsupported number of image channels: {}", actualChannels);
				break;
		}

		return std::make_pair(spec, data);
	}
}