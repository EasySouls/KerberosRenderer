#include "kbrpch.hpp"
#include "CubemapImporter.hpp"

#include "Renderer/TextureCube.hpp"
#include "Assets/Importers/TextureImporter.hpp"

#include <yaml-cpp/yaml.h>
#include <stb_image.h>

namespace Kerberos
{
	Ref<TextureCube> CubemapImporter::ImportCubemap(AssetHandle handle, const AssetMetadata& metadata)
	{
		return ImportCubemap(metadata.Filepath);
	}

	Ref<TextureCube> CubemapImporter::ImportCubemap(const std::filesystem::path& filepath)
	{
		///Imports a cubemap descriptor file, which contains paths to the six faces of the cubemap.

		CubemapDescriptor descriptor;

		const auto& absolutePath = std::filesystem::absolute(filepath);

		YAML::Node node;
		try
		{
			node = YAML::LoadFile(absolutePath.string());
		}
		catch (const YAML::Exception& e)
		{
			KBR_CORE_ERROR("CubemapImporter::ImportCubemap - Failed to load yaml file: {}", e.what());
			return nullptr;
		}

		if (auto cubemapNode = node["Cubemap"])
		{
			descriptor.Name = cubemapNode["Name"].as<std::string>();
			descriptor.RightPath = cubemapNode["RightFace"].as<std::string>();
			descriptor.LeftPath = cubemapNode["LeftFace"].as<std::string>();
			descriptor.TopPath = cubemapNode["TopFace"].as<std::string>();
			descriptor.BottomPath = cubemapNode["BottomFace"].as<std::string>();
			descriptor.FrontPath = cubemapNode["FrontFace"].as<std::string>();
			descriptor.BackPath = cubemapNode["BackFace"].as<std::string>();
			descriptor.IsSRGB = cubemapNode["IsSRGB"].as<bool>(false);
		}
		else
		{
			KBR_CORE_ERROR("CubemapImporter::ImportCubemap - Failed to load cubemap descriptor from file: {}", filepath.string());
			return nullptr;
		}

		CubemapData cubemapData;
		cubemapData.Name = descriptor.Name;
		cubemapData.IsSRGB = descriptor.IsSRGB;

		const auto loadFace = [](const std::filesystem::path& facePath) -> FaceData
		{
			FaceData faceData;

			constexpr int desiredChannels = 4;
			constexpr bool flip = true;

			const auto [spec, buffer] = TextureImporter::LoadTextureData(facePath, flip, desiredChannels);
			faceData.Specification = spec;
			faceData.Buffer = buffer;
			return faceData;
		};

		cubemapData.Faces[0] = loadFace(descriptor.RightPath);
		cubemapData.Faces[1] = loadFace(descriptor.LeftPath);
		cubemapData.Faces[2] = loadFace(descriptor.TopPath);
		cubemapData.Faces[3] = loadFace(descriptor.BottomPath);
		cubemapData.Faces[4] = loadFace(descriptor.FrontPath);
		cubemapData.Faces[5] = loadFace(descriptor.BackPath);

		Ref<TextureCube> cubemapTexture = TextureCube::Create(cubemapData);

		for (size_t i = 1; i < cubemapData.Faces.size(); ++i)
		{
			stbi_image_free(cubemapData.Faces[i].Buffer.Data);
		}

		if (!cubemapTexture)
		{
			KBR_CORE_ERROR("CubemapImporter::ImportCubemap - Failed to create cubemap texture from descriptor: {}", filepath.string());
			return nullptr;
		}

		cubemapTexture->SetDebugName(descriptor.Name);

		return cubemapTexture;
	}
}