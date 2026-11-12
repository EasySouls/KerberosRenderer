#include "Assets/Importers/TextureImporter.hpp"
#include "Renderer/Textures/KTX2FormatSelector.hpp"
#include "Renderer/Textures/MipmapGenerator.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace Kerberos
{
	TEST(TexturePipelineTest, CalculatesFullMipChain)
	{
		EXPECT_EQ(MipmapGenerator::CalculateMipLevelCount(1, 1), 1u);
		EXPECT_EQ(MipmapGenerator::CalculateMipLevelCount(2, 2), 2u);
		EXPECT_EQ(MipmapGenerator::CalculateMipLevelCount(3, 3), 2u);
		EXPECT_EQ(MipmapGenerator::CalculateMipLevelCount(1024, 512), 11u);
		EXPECT_EQ(MipmapGenerator::CalculateMipLevelCount(512, 1024), 11u);
	}

	TEST(TexturePipelineTest, SelectsSixteenBitAndFloatFormats)
	{
		EXPECT_EQ(KTX2FormatSelector::Select(ImageFormat::R16), vk::Format::eR16Unorm);
		EXPECT_EQ(KTX2FormatSelector::Select(ImageFormat::RG16), vk::Format::eR16G16Unorm);
		EXPECT_EQ(KTX2FormatSelector::Select(ImageFormat::RGB16), vk::Format::eR16G16B16Unorm);
		EXPECT_EQ(KTX2FormatSelector::Select(ImageFormat::RGBA16), vk::Format::eR16G16B16A16Unorm);
		EXPECT_EQ(KTX2FormatSelector::Select(ImageFormat::RGBA32F), vk::Format::eR32G32B32A32Sfloat);
	}

	TEST(TexturePipelineTest, ReinterpretsUnormColorFormatsAsSrgb)
	{
		EXPECT_EQ(KTX2FormatSelector::ToSrgb(vk::Format::eR8G8B8A8Unorm), vk::Format::eR8G8B8A8Srgb);
		EXPECT_EQ(KTX2FormatSelector::ToSrgb(vk::Format::eBc7UnormBlock), vk::Format::eBc7SrgbBlock);
		EXPECT_EQ(KTX2FormatSelector::ToSrgb(vk::Format::eR16G16B16A16Unorm), vk::Format::eR16G16B16A16Unorm);
	}

	TEST(TexturePipelineTest, LoadsHdrRadianceAsFloat)
	{
		const std::filesystem::path path =
			std::filesystem::temp_directory_path() / "kbr_texture_pipeline_hdr_test.hdr";

		{
			std::ofstream file(path, std::ios::binary);
			ASSERT_TRUE(file);
			file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
			constexpr unsigned char pixels[16] = {
				128, 64, 32, 128,
				64, 128, 32, 128,
				32, 64, 128, 128,
				128, 128, 128, 128
			};
			file.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
		}

		auto [spec, data] = TextureImporter::LoadTextureData(path, false, 0, true);
		ASSERT_TRUE(data.Data != nullptr);
		EXPECT_EQ(spec.Format, ImageFormat::RGBA32F);
		EXPECT_EQ(spec.Width, 2u);
		EXPECT_EQ(spec.Height, 2u);
		EXPECT_EQ(data.Size, 2ull * 2ull * 4ull * sizeof(float));

		const float* values = reinterpret_cast<const float*>(data.Data);
		EXPECT_GT(values[0], 0.0f);
		EXPECT_GT(values[1], 0.0f);

		std::error_code error;
		std::filesystem::remove(path, error);
	}
}
