#include "kbrpch.hpp"
#include "KTX2FormatSelector.hpp"

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
		default: return vk::Format::eR8G8B8A8Unorm;
	}
}

bool IsSupported(const vk::Format format)
{
	return format != vk::Format::eUndefined;
}

}
