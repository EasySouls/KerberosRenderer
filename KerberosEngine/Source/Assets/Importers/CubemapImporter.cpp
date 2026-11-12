#include "CubemapImporter.hpp"

#include "Renderer/Textures/TextureCube.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "ImportUtils.hpp"
#include "Utils/KtxConversion.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>

import Kerberos;

namespace Kerberos
{
	Ref<TextureCube> CubemapImporter::ImportCubemap(AssetHandle /*handle*/, const AssetMetadata& metadata)
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
			Log::CoreError("CubemapImporter::ImportCubemap - Failed to load yaml file: {}", e.what());
			return nullptr;
		}

		// A descriptor can either point at one KTX2 cubemap or list six faces.
		// The six-face form is what makes HDR (.hdr) environment authoring usable.
        if (node["Path"].IsDefined())
		{
			const std::filesystem::path texPath = node["Path"].as<std::string>();

			std::string extension = texPath.extension().string();
			std::ranges::transform(extension, extension.begin(), [](const unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});
			if (extension != ".ktx2")
			{
				Log::CoreError("CubemapImporter::ImportCubemap - single-file cubemap '{}' must be KTX2; "
					"use a Cubemap: descriptor with six face paths for HDR/LDR raster faces.", texPath.string());
				return nullptr;
			}

			if (NeedsConvertingToKTX2(texPath))
			{
				const Process::ProcessResult result = KtxConversion::ConvertKtxToKtx2(texPath);
				KBRAssert(
					result.Succeeded,
					"CubemapImporter::ImportCubemap - failed to convert KTX to KTX2 for file: {} (started: {}, exit code: {}, error code: {}, error: {})",
					texPath.string(),
					result.Started,
					result.ExitCode,
					result.ErrorCode,
					result.ErrorMessage);
			}

			const Ref<TextureCube> cubemapTexture = CreateRef<TextureCube>(texPath);
			if (!cubemapTexture)
			{
				Log::CoreError("CubemapImporter::ImportCubemap - Failed to create cubemap texture from file: {}", texPath.string());
				return nullptr;
			}
			return cubemapTexture;
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
			Log::CoreError("CubemapImporter::ImportCubemap - Failed to load cubemap descriptor from file: {}", filepath.string());
			return nullptr;
		}

		CubemapData cubemapData;
		cubemapData.Name = descriptor.Name;
		// Each face's TextureSpecification already carries the requested color
		// encoding (sRGB format for 8-bit color, linearized UNORM16 for 16-bit
		// color, or RGBA32F for HDR), so TextureCube must not reinterpret it again.
		cubemapData.IsSRGB = false;

		const auto loadFace = [](const std::filesystem::path& facePath, const bool srgb) -> FaceData
		{
			FaceData faceData;

			constexpr int desiredChannels = 4;
			constexpr bool flip = true;

			const auto [spec, buffer] = TextureImporter::LoadTextureData(facePath, flip, desiredChannels, srgb);
			faceData.Specification = spec;
			faceData.Buffer = Buffer::Copy(buffer);
			return faceData;
		};

		cubemapData.Faces[0] = loadFace(descriptor.RightPath, descriptor.IsSRGB);
		cubemapData.Faces[1] = loadFace(descriptor.LeftPath, descriptor.IsSRGB);
		cubemapData.Faces[2] = loadFace(descriptor.TopPath, descriptor.IsSRGB);
		cubemapData.Faces[3] = loadFace(descriptor.BottomPath, descriptor.IsSRGB);
		cubemapData.Faces[4] = loadFace(descriptor.FrontPath, descriptor.IsSRGB);
		cubemapData.Faces[5] = loadFace(descriptor.BackPath, descriptor.IsSRGB);

		Ref<TextureCube> cubemapTexture = CreateRef<TextureCube>(cubemapData);

		if (!cubemapTexture)
		{
			Log::CoreError("CubemapImporter::ImportCubemap - Failed to create cubemap texture from descriptor: {}", filepath.string());
			return nullptr;
		}

		cubemapTexture->SetDebugName(descriptor.Name);

		return cubemapTexture;
	}
}