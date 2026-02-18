#pragma once

#include "Vulkan.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kbr
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

		std::vector<vk::PipelineShaderStageCreateInfo> GetPipelineShaderStageCreateInfo() const;

	private:
		void Reflect();

	private:
		std::string m_Name;
		vk::raii::ShaderModule m_ShaderModule = nullptr;
		std::vector<uint32_t> m_SpirvCode;
		std::vector<ShaderStageEntry> m_StageEntries;
	};
}