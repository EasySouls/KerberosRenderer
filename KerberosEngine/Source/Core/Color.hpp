#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Kerberos
{
	/// The engine's authored color space is sRGB. Colors that come from glTF
	/// material factors or from physical material definitions are explicitly
	/// marked linear where they enter the engine.
	inline float SrgbToLinear(const float channel)
	{
		const float c = std::clamp(channel, 0.0f, 1.0f);
		return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}

	inline float LinearToSrgb(const float channel)
	{
		const float c = std::max(channel, 0.0f);
		return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
	}

	inline glm::vec3 SrgbToLinear(const glm::vec3& color)
	{
		return { SrgbToLinear(color.r), SrgbToLinear(color.g), SrgbToLinear(color.b) };
	}

	inline glm::vec3 LinearToSrgb(const glm::vec3& color)
	{
		return { LinearToSrgb(color.r), LinearToSrgb(color.g), LinearToSrgb(color.b) };
	}

	inline glm::vec4 SrgbToLinear(const glm::vec4& color)
	{
		return { SrgbToLinear(color.r), SrgbToLinear(color.g), SrgbToLinear(color.b), color.a };
	}

	inline glm::vec4 LinearToSrgb(const glm::vec4& color)
	{
		return { LinearToSrgb(color.r), LinearToSrgb(color.g), LinearToSrgb(color.b), color.a };
	}
}
