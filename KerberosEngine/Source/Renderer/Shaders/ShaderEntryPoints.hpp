#pragma once

namespace Kerberos
{
	enum class ShaderEntryPoint
	{
		Vertex,
		Geometry,
		Fragment,
		Compute,
	};

	static const char* GetEntryPointName(const ShaderEntryPoint entryPoint)
	{
		switch (entryPoint)
		{
		case ShaderEntryPoint::Vertex:
			return "vertexMain";
		case ShaderEntryPoint::Geometry:
			return "geometryMain";
		case ShaderEntryPoint::Fragment:
			return "fragmentMain";
		case ShaderEntryPoint::Compute:
			return "computeMain";
		default:
			throw std::runtime_error("Invalid shader entry point");
		}
	}

	static std::array s_AllShaderEntryPoints = {
		ShaderEntryPoint::Vertex,
		ShaderEntryPoint::Geometry,
		ShaderEntryPoint::Fragment,
		ShaderEntryPoint::Compute
	};
}