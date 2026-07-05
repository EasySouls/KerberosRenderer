#pragma once

namespace Kerberos
{
	enum class ShaderEntryPoint
	{
		Vertex,
		Geometry,
		Fragment,
		Compute,
		Task,
		Mesh
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
		case ShaderEntryPoint::Task:
			return "taskMain";
		case ShaderEntryPoint::Mesh:
			return "meshMain";
		default:
			throw std::runtime_error("Invalid shader entry point");
		}
	}

	static std::array s_AllShaderEntryPoints = {
		ShaderEntryPoint::Vertex,
		ShaderEntryPoint::Geometry,
		ShaderEntryPoint::Fragment,
		ShaderEntryPoint::Compute,
		ShaderEntryPoint::Task,
		ShaderEntryPoint::Mesh
	};
}