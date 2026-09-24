#include "Graph.hpp"

#include "VulkanContext.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>

import Kerberos;

namespace Kerberos::RenderGraph
{
	namespace
	{
		vk::ImageSubresourceRange DefaultRange()
		{
			return {
				.aspectMask = vk::ImageAspectFlagBits::eColor, 
				.baseMipLevel = 0, 
				.levelCount = vk::RemainingMipLevels, 
				.baseArrayLayer = 0, 
				.layerCount = vk::RemainingArrayLayers
            };
		}

		bool IsWrite(const ImageUsage& usage)
		{
			return usage.Usage == ResourceUsage::Write;
		}

		bool IsWrite(const BufferUsage& usage)
        {
            return usage.Usage == ResourceUsage::Write;
        }
	}

	Graph::PassBuilder& Graph::PassBuilder::Read(const ImageHandle image,
												 const vk::ImageLayout layout,
												 const vk::PipelineStageFlags2 stages, 
												 const vk::AccessFlags2 access, 
												 const vk::ImageSubresourceRange& range)
	{
		m_Graph.AddUsage(m_Pass, { .Image = image, .Usage = ResourceUsage::Read, .Layout = layout, .Stages = stages, .Access = access, .SubresourceRange = range });
		return *this;
	}

	Graph::PassBuilder& Graph::PassBuilder::Write(const ImageHandle image,
												  const vk::ImageLayout layout,
												  const vk::PipelineStageFlags2 stages,
												  const vk::AccessFlags2 access,
												  const vk::ImageSubresourceRange& range)
	{
		m_Graph.AddUsage(m_Pass, { .Image = image, .Usage = ResourceUsage::Write, .Layout = layout, .Stages = stages, .Access = access, .SubresourceRange = range });
		return *this;
	}

    Graph::PassBuilder& Graph::PassBuilder::Read(const BufferHandle buffer,
												 const vk::PipelineStageFlags2 stages,
												 const vk::AccessFlags2 access)
    {
        m_Graph.AddUsage(m_Pass, { .Buffer = buffer, .Usage = ResourceUsage::Read, .Stages = stages, .Access = access });
        return *this;
    }

    Graph::PassBuilder& Graph::PassBuilder::Write(BufferHandle buffer,
												  const vk::PipelineStageFlags2 stages,
												  const vk::AccessFlags2 access)
    {
        m_Graph.AddUsage(m_Pass,{ .Buffer = buffer, .Usage = ResourceUsage::Write, .Stages = stages, .Access = access });
        return *this;
    }

    ImageHandle Graph::ImportImage(const vk::Image image, const ImageState& state, const vk::ImageSubresourceRange& inputRange)
	{
		vk::ImageSubresourceRange range = inputRange;
		if (range.aspectMask == vk::ImageAspectFlags{})
			range = DefaultRange();
		m_Images.push_back({ .Image = image, .InitialState = state, .Range = range });
		return { static_cast<uint32_t>(m_Images.size() - 1) };
	}

    BufferHandle Graph::ImportBuffer(const vk::Buffer buffer, const BufferState& state)
    {
        m_Buffers.push_back({ .Buffer = buffer, .InitialState = state });
        return { static_cast<uint32_t>(m_Buffers.size() - 1) };
    }

	void Graph::AddUsage(const PassHandle pass, ImageUsage usage)
	{
		if (!pass || pass.Index >= m_Passes.size() || !usage.Image || usage.Image.Index >= m_Images.size())
		{
			throw std::out_of_range("Render graph resource or pass handle is invalid");
		}

		if (usage.SubresourceRange.aspectMask == vk::ImageAspectFlags{})
		{
			usage.SubresourceRange = m_Images[usage.Image.Index].Range;
		}

		m_Passes[pass.Index].ImageUsages.push_back(usage);
	}

    void Graph::AddUsage(const PassHandle pass, const BufferUsage& usage)
    {
        if (!pass || pass.Index >= m_Passes.size() || !usage.Buffer || usage.Buffer.Index >= m_Buffers.size()) {
            throw std::out_of_range("Render graph resource or pass handle is invalid");
        }

        m_Passes[pass.Index].BufferUsages.push_back(usage);
    }

    const std::vector<Graph::CompiledPass>& Graph::Compile()
	{
		const size_t passCount = m_Passes.size();
		std::vector<std::vector<uint32_t>> edges(passCount);
		std::vector<uint32_t> indegree(passCount, 0);

		for (uint32_t a = 0; a < passCount; ++a)
		{
			for (uint32_t b = a + 1; b < passCount; ++b)
			{
				bool dependency = false;
                for (const ImageUsage& left : m_Passes[a].ImageUsages) 
				{
                    for (const ImageUsage& right : m_Passes[b].ImageUsages) 
					{
                        if (left.Image == right.Image && (IsWrite(left) || IsWrite(right))) 
						{
							dependency = true;
                        }
					}
				}

				for (const auto& left : m_Passes[a].BufferUsages) 
				{
                    for (const auto& right : m_Passes[b].BufferUsages) 
					{
                        if (left.Buffer == right.Buffer && (IsWrite(left) || IsWrite(right))) 
						{
                            dependency = true;
                        
						}
                    }
                }

				if (dependency)
				{
					edges[a].push_back(b);
					++indegree[b];
				}
			}
		}

		std::queue<uint32_t> ready;
		for (uint32_t i = 0; i < passCount; ++i)
			if (indegree[i] == 0)
				ready.push(i);

		m_ExecutionOrder.clear();
		while (!ready.empty())
		{
			const uint32_t pass = ready.front();
			ready.pop();
			m_ExecutionOrder.push_back({ pass });
			for (uint32_t dependent : edges[pass])
				if (--indegree[dependent] == 0)
					ready.push(dependent);
		}
		if (m_ExecutionOrder.size() != passCount)
			throw std::logic_error("Render graph contains a dependency cycle");

		struct ImgState
        {
            ImageState Current{};
		    ImageState LastWrite{};
		    bool HasUse = false;
		    bool HasWrite = false;
		    bool WasWrite = false;
		};
        std::vector<ImgState> imgStates;
        imgStates.reserve(m_Images.size());
        for (const auto& img : m_Images)
        {
            imgStates.push_back({ .Current = img.InitialState, .LastWrite = img.InitialState,
                                  .HasUse = false, .HasWrite = false, .WasWrite = false });
        }

        struct BufState
        {
            BufferState Current{};
            bool HasUse = false;
            bool WasWrite = false;
        };
        std::vector<BufState> bufStates;
        bufStates.reserve(m_Buffers.size());
        for (const auto& [Buffer, InitialState] : m_Buffers)
        {
            bufStates.push_back({ .Current = InitialState, .HasUse = false, .WasWrite = false });
        }

        m_CompiledPasses.clear();
        for (PassHandle handle : m_ExecutionOrder) 
        {
            const Pass& pass = m_Passes[handle.Index];
            CompiledPass compiled{ .Handle = handle, .Name = pass.Name, .ImageBarriers = {}, .BufferBarriers = {} };

            // Image Barriers
            for (const ImageUsage& usage : pass.ImageUsages) 
            {
                ImgState& previous = imgStates[usage.Image.Index];
                const ImageState old = previous.Current;
                const bool isWrite = usage.Usage == ResourceUsage::Write;
                const vk::PipelineStageFlags2 sourceStages =
                    old.Stages | (previous.HasWrite ? previous.LastWrite.Stages : vk::PipelineStageFlags2{});
                const vk::AccessFlags2 sourceAccess =
                    old.Access | (previous.HasWrite ? previous.LastWrite.Access : vk::AccessFlags2{});

                if (!previous.HasUse || previous.WasWrite || isWrite || old.Layout != usage.Layout ||
                    old.Stages != usage.Stages || old.Access != usage.Access) 
                {
                    compiled.ImageBarriers.push_back({ .srcStageMask = sourceStages,
                                                       .srcAccessMask = sourceAccess,
                                                       .dstStageMask = usage.Stages,
                                                       .dstAccessMask = usage.Access,
                                                       .oldLayout = old.Layout,
                                                       .newLayout = usage.Layout,
                                                       .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                       .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                       .image = m_Images[usage.Image.Index].Image,
                                                       .subresourceRange = usage.SubresourceRange });
                }
                previous.Current = { .Layout = usage.Layout, .Stages = usage.Stages, .Access = usage.Access };
                previous.HasUse = true;
                if (isWrite)
                {
                    previous.LastWrite = previous.Current;
                    previous.HasWrite = true;
                }
                previous.WasWrite = isWrite;
            }

            // Buffer Barriers
            for (const BufferUsage& usage : pass.BufferUsages) 
            {
                BufState& previous = bufStates[usage.Buffer.Index];
                const BufferState old = previous.Current;
                const bool isWrite = usage.Usage == ResourceUsage::Write;

                if (!previous.HasUse || previous.WasWrite || isWrite || old.Stages != usage.Stages ||
                    old.Access != usage.Access) 
                {
                    compiled.BufferBarriers.push_back({ .srcStageMask = old.Stages,
                                                        .srcAccessMask = old.Access,
                                                        .dstStageMask = usage.Stages,
                                                        .dstAccessMask = usage.Access,
                                                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                        .buffer = m_Buffers[usage.Buffer.Index].Buffer,
                                                        .offset = 0,
                                                        .size = vk::WholeSize });
                }
                previous.Current = { .Stages = usage.Stages, .Access = usage.Access };
                previous.HasUse = true;
                previous.WasWrite = isWrite;
            }

            m_CompiledPasses.push_back(std::move(compiled));
        }
        return m_CompiledPasses;
	}

	void Graph::Clear()
	{
		m_Images.clear();
        m_Buffers.clear();
		m_Passes.clear();
		m_ExecutionOrder.clear();
		m_CompiledPasses.clear();
	}

    void Graph::Execute(const vk::raii::CommandBuffer& cmd)
    {
        for (const auto& [Handle, Name, ImageBarriers, BufferBarriers] : m_CompiledPasses)
        {
            BeginRenderPassDebugLabel(cmd, Name);

            if (!ImageBarriers.empty() || !BufferBarriers.empty()) {
                vk::DependencyInfo dependency{};
                dependency.imageMemoryBarrierCount = static_cast<uint32_t>(ImageBarriers.size());
                dependency.pImageMemoryBarriers = ImageBarriers.data();
                dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(BufferBarriers.size());
                dependency.pBufferMemoryBarriers = BufferBarriers.data();

                cmd.pipelineBarrier2(dependency);
            }

            KBRAssert(m_Passes[Handle.Index].Exec != nullptr, "Pass execution function is not set for pass: {}", Name);

            m_Passes[Handle.Index].Exec(cmd);

            EndRenderPassDebugLabel(cmd);
        }
    }
}
