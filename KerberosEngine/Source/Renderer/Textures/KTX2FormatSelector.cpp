#include "KTX2FormatSelector.hpp"

import Kerberos;

namespace Kerberos::KTX2FormatSelector {

vk::Format Select(const ImageFormat format)
{
	switch (format)
	{
		case ImageFormat::R8: return vk::Format::eR8Unorm;
		case ImageFormat::RG8: return vk::Format::eR8G8Unorm;
		case ImageFormat::RGB8: return vk::Format::eR8G8B8Unorm;
		case ImageFormat::RGBA8: return vk::Format::eR8G8B8A8Unorm;
		case ImageFormat::RGBA32F: return vk::Format::eR32G32B32A32Sfloat;
        case ImageFormat::SRGB8: return vk::Format::eR8G8B8Srgb;
        case ImageFormat::SRGBA8: return vk::Format::eR8G8B8A8Srgb;
        default: {
            KBRAssert("KTX2FormatSelector::Select - Unsupported ImageFormat: {}", static_cast<int>(format));
	        return vk::Format::eR8G8B8A8Unorm;
        }
	}
}

bool IsSupported(const vk::Format format)
{
	return format != vk::Format::eUndefined;
}

ImageFormat ToSrgb(const ImageFormat format)
{
	switch (format)
	{
		case ImageFormat::RGB8: return ImageFormat::SRGB8;
		case ImageFormat::RGBA8: return ImageFormat::SRGBA8;
		default: return format;
	}
}

vk::Format ToSrgb(const vk::Format format)
{
	switch (format)
	{
		case vk::Format::eR8G8B8Unorm: return vk::Format::eR8G8B8Srgb;
		case vk::Format::eR8G8B8A8Unorm: return vk::Format::eR8G8B8A8Srgb;
		case vk::Format::eB8G8R8A8Unorm: return vk::Format::eB8G8R8A8Srgb;
		default: return format;
	}
}

}
