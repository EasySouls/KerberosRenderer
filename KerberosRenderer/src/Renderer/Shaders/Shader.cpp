#include "pch.hpp"
#include "Shader.hpp"

#include "io.hpp"
#include "VulkanContext.hpp"

#include <spirv_cross/spirv_cross.hpp>

#include "SlangCompiler.hpp"
#include "logging/Log.hpp"

namespace kbr
{
	namespace Utils 
	{

		static const char* GetCacheDirectory()
		{
#ifdef KBR_DEBUG
			return "assets/cache/shaders/debug";
#else
			return "assets/cache/shaders";
#endif
		}

		static void CreateCacheDirectoryIfNeeded()
		{
			const std::string cacheDirectory = GetCacheDirectory();
			if (!std::filesystem::exists(cacheDirectory))
				std::filesystem::create_directories(cacheDirectory);
		}

	}

	static vk::ShaderStageFlagBits ExecutionModelToShaderStage(const spv::ExecutionModel model)
	{
		switch (model)
		{
			case spv::ExecutionModelVertex:                 return vk::ShaderStageFlagBits::eVertex;
			case spv::ExecutionModelTessellationControl:    return vk::ShaderStageFlagBits::eTessellationControl;
			case spv::ExecutionModelTessellationEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
			case spv::ExecutionModelGeometry:               return vk::ShaderStageFlagBits::eGeometry;
			case spv::ExecutionModelFragment:               return vk::ShaderStageFlagBits::eFragment;
			case spv::ExecutionModelGLCompute:              return vk::ShaderStageFlagBits::eCompute;
			default:
				KBR_CORE_ERROR("Unsupported SPIR-V execution model: {}", static_cast<int>(model));
				return vk::ShaderStageFlagBits::eVertex;
		}
	}

	Shader::Shader(const std::filesystem::path& filepath, std::string name)
		: m_Name(std::move(name))
	{
		Utils::CreateCacheDirectoryIfNeeded();

		std::filesystem::path shadersPath = std::filesystem::path("assets") / "shaders";
		std::filesystem::path cacheDirectory = Utils::GetCacheDirectory();
		m_Filepath = shadersPath / (filepath.string() + std::string(".slang"));

		/// Get source file modification time
		std::filesystem::file_time_type sourceModTime{};
		if (!filepath.empty() && std::filesystem::exists(m_Filepath))
		{
			sourceModTime = std::filesystem::last_write_time(m_Filepath);
		}

		bool needsCompilation = true;
		const std::filesystem::path cachedSpirvPath = cacheDirectory / (filepath.stem().string() + ".spv");
		if (std::filesystem::exists(cachedSpirvPath)) 
		{
			const std::filesystem::file_time_type cachedModTime = std::filesystem::last_write_time(cachedSpirvPath);
			if (cachedModTime >= sourceModTime) 
			{
				KBR_CORE_INFO("Loading cached SPIR-V for shader: {}", filepath.filename().string());
				needsCompilation = false;
			} 
			else 
			{
				KBR_CORE_INFO("Cached SPIR-V is outdated. Recompiling shader: {}", filepath.filename().string());
				needsCompilation = true;
			}
		} 
		else 
		{
			KBR_CORE_INFO("No cached SPIR-V found. Compiling shader: {}", filepath.filename().string());
			needsCompilation = true;
			
		}

		if (needsCompilation) 
		{
			try
			{
				m_SpirvCode = SlangCompiler::CompileToSpirv(m_Filepath);

				std::ofstream outFile(cachedSpirvPath, std::ios::binary);
				outFile.write(reinterpret_cast<const char*>(m_SpirvCode.data()), static_cast<std::streamsize>(m_SpirvCode.size() * sizeof(uint32_t)));
			}
			catch (const CompilationFailedException& e)
			{
				KBR_CORE_ERROR("Shader compilation failed for '{}': {}", filepath.filename().string(), e.what());
				throw;
			}
			catch (const std::exception& e)
			{
				KBR_CORE_ERROR("Failed to compile shader '{}': {}", filepath.filename().string(), e.what());
				throw;
			}
		}
		else
		{
			const auto shaderCode = IO::ReadFile(cachedSpirvPath);
			m_SpirvCode.resize(shaderCode.size() / sizeof(uint32_t));
			std::memcpy(m_SpirvCode.data(), shaderCode.data(), shaderCode.size());
		}
		
		if (m_SpirvCode.empty()) 
		{
			KBR_CORE_ERROR("Failed to load SPIR-V code for shader: {}", filepath.filename().string());
			throw std::runtime_error("Failed to load SPIR-V code");
		}

		const vk::ShaderModuleCreateInfo shaderInfo{
			.codeSize = m_SpirvCode.size() * sizeof(uint32_t),
			.pCode = m_SpirvCode.data()
		};

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		m_ShaderModule = vk::raii::ShaderModule{ device, shaderInfo };

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkShaderModule>(*m_ShaderModule)),
								   vk::ObjectType::eShaderModule,
								   m_Name + "_ShaderModule");

		Reflect();
	}

	std::vector<vk::PipelineShaderStageCreateInfo> Shader::GetPipelineShaderStageCreateInfo() const 
	{
		std::vector<vk::PipelineShaderStageCreateInfo> stages;
		stages.reserve(m_StageEntries.size());

		for (const auto& [stage, entryPoint] : m_StageEntries) {
			stages.push_back(vk::PipelineShaderStageCreateInfo{
				.stage = stage,
				.module = m_ShaderModule,
				.pName = entryPoint.c_str()
			});
		}

		return stages;
	}

	void Shader::Reflect() 
	{
		const spirv_cross::Compiler compiler(m_SpirvCode);
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		KBR_CORE_INFO("Reflecting shader {}", m_Name);

		auto entryPoints = compiler.get_entry_points_and_stages();
		m_StageEntries.clear();
		m_StageEntries.reserve(entryPoints.size());

		for (const auto& [name, execution_model] : entryPoints) {
			const vk::ShaderStageFlagBits stage = ExecutionModelToShaderStage(execution_model);
			m_StageEntries.push_back({ .stage = stage, .entryPoint = name });
			KBR_CORE_INFO("  Entry point: {0}, Stage: {1}", name, vk::to_string(stage));
		}

		KBR_CORE_INFO("    Uniform Buffers: {0}", resources.uniform_buffers.size());
		for (const auto& resource : resources.uniform_buffers) {
			const auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			size_t bufferSize = compiler.get_declared_struct_size(bufferType);

			KBR_CORE_INFO("      Name: {0}, Set: {1}, Binding: {2}, Size: {3}", resource.name, set, binding, bufferSize);

			// List members
			for (uint32_t i = 0; i < bufferType.member_types.size(); ++i) {
				std::string memberName = compiler.get_member_name(resource.base_type_id, i);
				size_t memberSize = compiler.get_declared_struct_member_size(bufferType, i);
				uint32_t offset = compiler.type_struct_member_offset(bufferType, i);
				KBR_CORE_INFO("        Member: {0}, Offset: {1}, Size: {2}", memberName, offset, memberSize);
			}
		}

		KBR_CORE_INFO("    Sampled Images (Textures/Samplers): {0}", resources.sampled_images.size());
		for (const auto& resource : resources.sampled_images) {
			uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t descriptorCount = 1;

			// Check if it's an array of textures (e.g., `sampler2D textures[4]`)
			const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
			if (!type.array.empty()) {
				descriptorCount = type.array[0]; // Assuming 1D array for simplicity
				if (descriptorCount == 0) // Unsized array (e.g., `sampler2D textures[]`)
					//descriptorCount = VulkanContext::Get().GetCapabilities().maxSamplerAllocationCount; // Or some max you define
					descriptorCount = 1; // Default to 1 if unsized
			}

			KBR_CORE_INFO("      Name: {0}, Set: {1}, Binding: {2}, Count: {3}", resource.name, set, binding, descriptorCount);
		}

		KBR_CORE_INFO("    Storage Buffers: {0}", resources.storage_buffers.size());
		for (const auto& resource : resources.storage_buffers) {
			const auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			size_t bufferSize = compiler.get_declared_struct_size(bufferType);

			KBR_CORE_INFO("      Name: {0}, Set: {1}, Binding: {2}, Approx Size: {3}", resource.name, set, binding, bufferSize);
		}

		KBR_CORE_INFO("    Storage Images: {0}", resources.storage_images.size());
		for (const auto& resource : resources.storage_images) {
			uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);

			KBR_CORE_INFO("      Name: {0}, Set: {1}, Binding: {2}", resource.name, set, binding);
		}

		KBR_CORE_INFO("    Push Constant Buffers: {0}", resources.push_constant_buffers.size());
		for (const auto& resource : resources.push_constant_buffers) {
			const auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t offset = 0; // Typically
			// SPIRV-Cross might need a bit more work to get exact offset for members if it's a struct.
		   // For a single push constant block, its range covers the whole block.
			size_t size = compiler.get_declared_struct_size(bufferType);

			// Get shader stage for this push constant more accurately
			// auto ranges = compiler.get_active_buffer_ranges(resource.id);
			// For now, we assume the 'stage' passed to Reflect applies.

			KBR_CORE_INFO("      Name: {0}, Offset: {1}, Size: {2}", resource.name, offset, size);
		}
	}
}
