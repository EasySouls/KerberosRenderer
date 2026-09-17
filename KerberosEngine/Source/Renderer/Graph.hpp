#pragma once

#include "Vulkan.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Kerberos::RenderGraph
{
	struct ImageState
	{
		vk::ImageLayout Layout = vk::ImageLayout::eUndefined;
		vk::PipelineStageFlags2 Stages = vk::PipelineStageFlagBits2::eNone;
		vk::AccessFlags2 Access = vk::AccessFlagBits2::eNone;
	};

	struct ImageHandle
	{
		uint32_t Index = std::numeric_limits<uint32_t>::max();

		explicit operator bool() const { return Index != std::numeric_limits<uint32_t>::max(); }
		friend bool operator==(ImageHandle, ImageHandle) = default;
	};

	enum class ResourceUsage : uint8_t
	{
		Read,
		Write
	};

	struct ImageUsage
	{
		ImageHandle Image{};
		ResourceUsage Usage = ResourceUsage::Read;
		vk::ImageLayout Layout = vk::ImageLayout::eGeneral;
		vk::PipelineStageFlags2 Stages = vk::PipelineStageFlagBits2::eAllCommands;
		vk::AccessFlags2 Access = vk::AccessFlagBits2::eMemoryRead;
		vk::ImageSubresourceRange SubresourceRange{
            vk::ImageAspectFlagBits::eColor, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers
		};
	};

	struct PassHandle
	{
		uint32_t Index = std::numeric_limits<uint32_t>::max();

		explicit operator bool() const { return Index != std::numeric_limits<uint32_t>::max(); }
	};

	class Graph
	{
	public:
		struct CompiledPass
		{
			PassHandle Handle{};
			std::string Name;
			std::vector<vk::ImageMemoryBarrier2> ImageBarriers;
		};

		class PassBuilder
		{
		public:
			PassBuilder& Read(ImageHandle image,
				vk::ImageLayout layout,
				vk::PipelineStageFlags2 stages,
				vk::AccessFlags2 access,
				const vk::ImageSubresourceRange& range = {});
			PassBuilder& Write(ImageHandle image,
				vk::ImageLayout layout,
				vk::PipelineStageFlags2 stages,
				vk::AccessFlags2 access,
				const vk::ImageSubresourceRange& range = {});

		private:
			friend class Graph;
			PassBuilder(Graph& graph, const PassHandle pass) : m_Graph(graph), m_Pass(pass) {}
			Graph& m_Graph;
			PassHandle m_Pass;
		};

		ImageHandle ImportImage(vk::Image image, const ImageState& state,
			const vk::ImageSubresourceRange& inputRange = {});
		PassBuilder AddPass(std::string_view name);

		const std::vector<CompiledPass>& Compile();
		void Clear();

		void EmitBarriers(const vk::raii::CommandBuffer& commandBuffer, PassHandle pass) const;
		const std::vector<PassHandle>& GetExecutionOrder() const { return m_ExecutionOrder; }
		const std::vector<CompiledPass>& GetCompiledPasses() const { return m_CompiledPasses; }

	private:
		void AddUsage(PassHandle pass, ImageUsage usage);

		struct ImageResource
		{
			vk::Image Image = nullptr;
			ImageState InitialState{};
			vk::ImageSubresourceRange Range{};
		};

		struct Pass
		{
			std::string Name;
			std::vector<ImageUsage> Usages;
		};

		std::vector<ImageResource> m_Images;
		std::vector<Pass> m_Passes;
		std::vector<PassHandle> m_ExecutionOrder;
		std::vector<CompiledPass> m_CompiledPasses;
	};
}
