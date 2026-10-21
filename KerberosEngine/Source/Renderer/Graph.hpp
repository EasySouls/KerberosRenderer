#pragma once

#include "Vulkan.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <utility>

namespace Kerberos::RenderGraph
{
	struct ImageState
	{
		vk::ImageLayout Layout = vk::ImageLayout::eUndefined;
		vk::PipelineStageFlags2 Stages = vk::PipelineStageFlagBits2::eNone;
		vk::AccessFlags2 Access = vk::AccessFlagBits2::eNone;
	};

	struct BufferState
    {
        vk::PipelineStageFlags2 Stages = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2 Access = vk::AccessFlagBits2::eNone;
    };

	struct ImageHandle
    {
        uint32_t Index = std::numeric_limits<uint32_t>::max();

        explicit operator bool() const
        {
            return Index != std::numeric_limits<uint32_t>::max();
        }

        friend bool operator==(ImageHandle, ImageHandle) = default;
    };

	struct BufferHandle
    {
        uint32_t Index = std::numeric_limits<uint32_t>::max();

        explicit operator bool() const
        {
            return Index != std::numeric_limits<uint32_t>::max();
        }

        friend bool operator==(BufferHandle, BufferHandle) = default;
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

	struct BufferUsage
    {
        BufferHandle Buffer{};
        ResourceUsage Usage = ResourceUsage::Read;
        vk::PipelineStageFlags2 Stages = vk::PipelineStageFlagBits2::eAllCommands;
        vk::AccessFlags2 Access = vk::AccessFlagBits2::eMemoryRead;
    };

	struct PassHandle
	{
		uint32_t Index = std::numeric_limits<uint32_t>::max();

		explicit operator bool() const { return Index != std::numeric_limits<uint32_t>::max(); }
	};

	using PassExecuteFunction = std::move_only_function<void(const vk::raii::CommandBuffer&)>;

	class Graph
	{
	public:
		struct CompiledPass
		{
			PassHandle Handle{};
			std::string Name;
            std::vector<vk::ImageMemoryBarrier2> ImageBarriers;
            std::vector<vk::BufferMemoryBarrier2> BufferBarriers;
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

			PassBuilder& Read(BufferHandle buffer, vk::PipelineStageFlags2 stages, vk::AccessFlags2 access);
            PassBuilder& Write(BufferHandle buffer, vk::PipelineStageFlags2 stages, vk::AccessFlags2 access);

		private:
			friend class Graph;
			PassBuilder(Graph& graph, const PassHandle pass) : m_Graph(graph), m_Pass(pass) {}
			Graph& m_Graph;
			PassHandle m_Pass;
		};

		ImageHandle ImportImage(vk::Image image, const ImageState& state, const vk::ImageSubresourceRange& inputRange = {});
        BufferHandle ImportBuffer(vk::Buffer buffer, const BufferState& state);

		template <typename SetupFunc>
		void AddPass(const std::string_view name, SetupFunc&& setup)
		{
            const PassHandle handle{ static_cast<uint32_t>(m_Passes.size()) };
            m_Passes.push_back({ .Name = std::string(name) });

            PassBuilder builder(*this, handle);

            m_Passes.back().Exec = std::forward<SetupFunc>(setup)(builder);
		}

		const std::vector<CompiledPass>& Compile();
		void Clear();

		void Execute(const vk::raii::CommandBuffer& cmd);

	private:
		void AddUsage(PassHandle pass, ImageUsage usage);
        void AddUsage(PassHandle pass, const BufferUsage& usage);

		struct ImageResource
		{
			vk::Image Image = nullptr;
			ImageState InitialState{};
			vk::ImageSubresourceRange Range{};
		};

		struct BufferResource
        {
            vk::Buffer Buffer = nullptr;
            BufferState InitialState{};
        };

		struct Pass
		{
			std::string Name;
            std::vector<ImageUsage> ImageUsages{};
            std::vector<BufferUsage> BufferUsages{};
            PassExecuteFunction Exec = nullptr;
		};

		std::vector<ImageResource> m_Images;
        std::vector<BufferResource> m_Buffers;
		std::vector<Pass> m_Passes;
		std::vector<PassHandle> m_ExecutionOrder;
		std::vector<CompiledPass> m_CompiledPasses;
	};
}
