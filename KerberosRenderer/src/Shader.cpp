#include "pch.hpp"
#include "Shader.hpp"

#include "io.hpp"
#include "VulkanContext.hpp"

#include <spirv_cross/spirv_cross.hpp>

#include "logging/Log.hpp"

namespace kbr
{
	Shader::Shader(const std::filesystem::path& filepath, std::string name)
		: m_Name(std::move(name))
	{
		const auto shaderCode = IO::ReadFile(filepath);
		m_SpirvCode.resize(shaderCode.size() / sizeof(uint32_t));
		std::memcpy(m_SpirvCode.data(), shaderCode.data(), shaderCode.size());

		const vk::ShaderModuleCreateInfo shaderInfo{
			.codeSize = shaderCode.size() * sizeof(char),
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
		const vk::PipelineShaderStageCreateInfo vertShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eVertex, .module = m_ShaderModule,  .pName = "vertMain" };
		const vk::PipelineShaderStageCreateInfo fragShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = m_ShaderModule, .pName = "fragMain" };
		return { vertShaderStageInfo, fragShaderStageInfo };
	}

	void Shader::Reflect() 
	{
		const spirv_cross::Compiler compiler(m_SpirvCode);
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		KBR_CORE_INFO("Reflecting shader {}", m_Name);

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
