#include "Shader.hpp"
#include "Core/Core.hpp"

#include "IO.hpp"
#include "VulkanContext.hpp"
#include "SlangCompiler.hpp"
#include "Profiling/Instrumentor.hpp"

#include <spirv_cross/spirv_cross.hpp>

#include <format>
#include <iostream>

import Kerberos;

namespace Kerberos
{
	namespace Utils 
	{

		static const char* GetCacheDirectory()
		{
#ifdef KBR_DEBUG
			return "Assets/Cache/Shaders/Debug";
#else
			return "Assets/Cache/Shaders";
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
				Log::CoreError("Unsupported SPIR-V execution model: {}", static_cast<int>(model));
				return vk::ShaderStageFlagBits::eVertex;
		}
	}

	static void ReflectStructMembers(const spirv_cross::Compiler& compiler, const uint32_t typeId, const std::string_view indent)
	{
		const spirv_cross::SPIRType& type = compiler.get_type(typeId);

		for (uint32_t i = 0; i < type.member_types.size(); ++i)
		{
			const uint32_t memberTypeId = type.member_types[i];
			const spirv_cross::SPIRType& memberType = compiler.get_type(memberTypeId);

			const std::string& memberName = compiler.get_member_name(typeId, i);
			size_t memberSize = compiler.get_declared_struct_member_size(type, i);
			const uint32_t offset = compiler.type_struct_member_offset(type, i);

			std::string arrayInfo;
			if (!memberType.array.empty())
			{
				if (memberType.array[0] == 0)
				{
					const size_t stride = compiler.type_struct_member_array_stride(type, i);
					arrayInfo = std::format("[] (Runtime Array, Stride: {})", stride);
					memberSize = 0; // Size is dynamic
				}
				else
				{
					arrayInfo = std::format("[{}] (Stride: {})", memberType.array[0], compiler.type_struct_member_array_stride(type, i));
				}
			}

			std::string typeName = "Unknown";
			switch (memberType.basetype)
			{
				case spirv_cross::SPIRType::Struct: typeName = "Struct"; break;
				case spirv_cross::SPIRType::Float:  typeName = "Float"; break;
				case spirv_cross::SPIRType::Int:    typeName = "Int"; break;
				case spirv_cross::SPIRType::UInt:   typeName = "UInt"; break;
				case spirv_cross::SPIRType::Boolean:typeName = "Bool"; break;
				// TODO:  Add Vector/Matrix checks using member_type.vecsize / columns
				default: break;
			}

			Log::CoreInfo("{0}Member: {1}{2}, Type: {3}, Offset: {4}, Size: {5}",
						  indent, memberName, arrayInfo, typeName, offset, memberSize);

			if (memberType.basetype == spirv_cross::SPIRType::Struct)
			{
				const std::string_view newIndent = std::string_view(indent).substr(0, indent.size() + 1);
				ReflectStructMembers(compiler, memberTypeId, newIndent);
			}
		}
	}

	static uint32_t GetDescriptorArraySize(const spirv_cross::Compiler& compiler, const spirv_cross::Resource& resource)
	{
		const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
		if (!type.array.empty())
		{
			return type.array[0] == 0 ? 1 : type.array[0];
		}
		return 1;
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
				Log::CoreInfo("Loading cached SPIR-V for shader: {}", filepath.filename().string());
				needsCompilation = false;
			} 
			else 
			{
				Log::CoreInfo("Cached SPIR-V is outdated. Recompiling shader: {}", filepath.filename().string());
				needsCompilation = true;
			}
		} 
		else 
		{
			Log::CoreInfo("No cached SPIR-V found. Compiling shader: {}", filepath.filename().string());
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
				Log::CoreError("Shader compilation failed for '{}': {}", filepath.filename().string(), e.what());
				throw;
			}
			catch (const std::exception& e)
			{
				Log::CoreError("Failed to compile shader '{}': {}", filepath.filename().string(), e.what());
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
			Log::CoreError("Failed to load SPIR-V code for shader: {}", filepath.filename().string());
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

		m_StageEntries = Reflect();
	}

	std::expected<bool, std::string> Shader::Recompile() 
	{
		KBR_PROFILE_FUNCTION();

		try 
		{
			const auto spirvCode = SlangCompiler::CompileToSpirv(m_Filepath);
			if (spirvCode.empty()) 
			{
				Log::CoreError("Recompilation produced empty SPIR-V code for shader: {}", m_Filepath.filename().string());
				return std::unexpected("Recompilation produced empty SPIR-V code");
			}

			if (spirvCode == m_SpirvCode)
			{
				Log::CoreInfo("Shader '{}' is already up to date. No recompilation needed.", m_Filepath.filename().string());
				return false;
			}

			m_SpirvCode = spirvCode;

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

			m_StageEntries = Reflect();
		}
		catch (const CompilationFailedException& e) 
		{
			Log::CoreError("Shader recompilation failed for '{}': {}", m_Filepath.filename().string(), e.what());
			return std::unexpected("Shader recompilation failed");
		} 
		catch (const std::exception& e) 
		{
			Log::CoreError("Failed to recompile shader '{}': {}", m_Filepath.filename().string(), e.what());
			return std::unexpected("Failed to recompile shader");
		}

		return true;
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

	std::vector<ShaderStageEntry> Shader::Reflect()
	{
		using namespace spirv_cross;

		const Compiler compiler(m_SpirvCode);
		const ShaderResources resources = compiler.get_shader_resources();

		Log::CoreInfo("Reflecting shader {}", m_Name);

		const auto entryPoints = compiler.get_entry_points_and_stages();

		std::vector<ShaderStageEntry> stageEntries;
		stageEntries.reserve(entryPoints.size());

		constexpr std::string_view threeSpaces{"   "};

		for (const auto& [name, execution_model] : entryPoints) {
			const vk::ShaderStageFlagBits stage = ExecutionModelToShaderStage(execution_model);
			stageEntries.push_back({ .stage = stage, .entryPoint = name });
			Log::CoreInfo("  Entry point: {0}, Stage: {1}", name, vk::to_string(stage));
		}

		Log::CoreInfo(" Uniform Buffers: {0}", resources.uniform_buffers.size());
		for (const Resource& resource : resources.uniform_buffers) {
			const SPIRType& bufferType = compiler.get_type(resource.base_type_id);
			const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			const size_t bufferSize = compiler.get_declared_struct_size(bufferType);

			Log::CoreInfo("  Name: {0}, Set: {1}, Binding: {2}, Size: {3}", resource.name, set, binding, bufferSize);

			ReflectStructMembers(compiler, resource.base_type_id, threeSpaces);
		}

		Log::CoreInfo(" Storage Buffers: {0}", resources.storage_buffers.size());
		for (const Resource& resource : resources.storage_buffers)
		{
			const SPIRType& bufferType = compiler.get_type(resource.base_type_id);
			const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			const size_t bufferSize = compiler.get_declared_struct_size_runtime_array(bufferType, 0);

			Log::CoreInfo("  Name: {0}, Set: {1}, Binding: {2}, Base Size: {3}", resource.name, set, binding, bufferSize);

			ReflectStructMembers(compiler, resource.base_type_id, threeSpaces);
		}

		auto reflectImageSampler = [&](const auto& resourceList, const char* label)
		{
			Log::CoreInfo(" {0}: {1}", label, resourceList.size());
			for (const Resource& resource : resourceList)
			{
				const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
				const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
				const uint32_t count = GetDescriptorArraySize(compiler, resource);
				Log::CoreInfo("  Name: {0}, Set: {1}, Binding: {2}, Count: {3}", resource.name, set, binding, count);
			}
		};

		reflectImageSampler(resources.sampled_images, "Sampled Images");
		reflectImageSampler(resources.separate_images, "Images");
		reflectImageSampler(resources.separate_samplers, "Samplers");

		Log::CoreInfo(" Storage Images: {0}", resources.storage_images.size());
		for (const Resource& resource : resources.storage_images) {
			const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			const SPIRType& type = compiler.get_type(resource.base_type_id);

			const char* dimStr = "Unknown";
			switch (type.image.dim) {
				case spv::Dim1D:   dimStr = "1D"; break;
				case spv::Dim2D:   dimStr = "2D"; break;
				case spv::Dim3D:   dimStr = "3D"; break;
				case spv::DimCube: dimStr = "Cube"; break;
				default:           dimStr = "Unknown"; break;
			}
			Log::CoreInfo("  Name: {0}, Set: {1}, Binding: {2}, Type: {3} Image", resource.name, set, binding, dimStr);
		}

		Log::CoreInfo(" Push Constant Buffers: {0}", resources.push_constant_buffers.size());
		for (const Resource& resource : resources.push_constant_buffers) {
			auto activeRanges = compiler.get_active_buffer_ranges(resource.id);

			for (const auto& range : activeRanges)
			{
				Log::CoreInfo("  Name: {0}, Active Offset: {1}, Active Size: {2}", resource.name, range.offset, range.range);
			}

			ReflectStructMembers(compiler, resource.base_type_id, threeSpaces);
		}

		Log::CoreInfo(" Acceleration Structures: {0}", resources.acceleration_structures.size());
		for (const Resource& resource : resources.acceleration_structures) {
			const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			Log::CoreInfo("  Name: {0}, Set: {1}, Binding: {2}", resource.name, set, binding);
		}

		return stageEntries;
	}
}