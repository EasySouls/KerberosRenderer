#pragma once

#include "Vulkan.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <expected>

namespace Kerberos
{
	struct ShaderStageEntry
	{
		vk::ShaderStageFlagBits stage;
		std::string entryPoint;
	};

	class Shader
	{
	public:
		explicit Shader(const std::filesystem::path& filepath, std::string name);
		virtual ~Shader() = default;

		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;
		Shader(Shader&&) = default;
		Shader& operator=(Shader&&) = default;

		/*
		* Recompiles the shader if the source file has been modified since the last compilation.
		* Returns true if the shader was successfully recompiled, false if recompilation was not needed, and an error message if recompilation failed.
		*/
		std::expected<bool, std::string> Recompile();

		std::vector<vk::PipelineShaderStageCreateInfo> GetPipelineShaderStageCreateInfo() const;

	private:
		std::vector<ShaderStageEntry> Reflect();

	private:
		std::string m_Name;
		vk::raii::ShaderModule m_ShaderModule = nullptr;
		std::vector<uint32_t> m_SpirvCode;
		std::vector<ShaderStageEntry> m_StageEntries;
		std::filesystem::path m_Filepath;
	};
}