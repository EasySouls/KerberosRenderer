#pragma once

#include "Vulkan.hpp"
#include "ShaderResourceSet.hpp"

#include <vector>

namespace Kerberos
{
	class DescriptorManager
	{
	public:
		static void BindSets(const vk::raii::CommandBuffer& cmd, vk::PipelineBindPoint bindPoint,
		                     const vk::raii::PipelineLayout& pipelineLayout, uint32_t firstSet,
		                     const std::vector<ShaderResourceSet>& sets);
	};
}