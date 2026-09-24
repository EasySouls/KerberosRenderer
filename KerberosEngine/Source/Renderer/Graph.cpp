#include "Graph.hpp"

#include "VulkanContext.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>

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

		struct ImageSubresource
		{
			uint32_t Image = 0;
			uint32_t Mip = 0;
			uint32_t Layer = 0;

			bool operator==(const ImageSubresource& other) const
			{
				return Image == other.Image && Mip == other.Mip && Layer == other.Layer;
			}
		};

		struct ImageSubresourceHash
		{
			size_t operator()(const ImageSubresource& subresource) const
			{
				size_t hash = std::hash<uint32_t>{}(subresource.Image);
				hash = hash * 31 + std::hash<uint32_t>{}(subresource.Mip);
				return hash * 31 + std::hash<uint32_t>{}(subresource.Layer);
			}
		};

		struct ImgState
        {
            ImageState Current{};
		    ImageState LastWrite{};
		    bool HasUse = false;
		    bool HasWrite = false;
		    bool WasWrite = false;
		};
        std::unordered_map<ImageSubresource, ImgState, ImageSubresourceHash> imgStates;

        struct BufState
        {
            BufferState Current{};
            BufferState LastWrite{};
            bool HasUse = false;
            bool HasWrite = false;
            bool WasWrite = false;
        };
        std::vector<BufState> bufStates;
        bufStates.reserve(m_Buffers.size());
        for (const auto& [Buffer, InitialState] : m_Buffers)
        {
            bufStates.push_back({ .Current = InitialState, .LastWrite = InitialState,
                                  .HasUse = false, .HasWrite = false, .WasWrite = false });
        }

        m_CompiledPasses.clear();
        for (PassHandle handle : m_ExecutionOrder) 
        {
            const Pass& pass = m_Passes[handle.Index];
            CompiledPass compiled{ .Handle = handle, .Name = pass.Name, .ImageBarriers = {}, .BufferBarriers = {} };

            // Image Barriers
            struct MergedImageUsage
            {
                vk::ImageAspectFlags AspectMask{};
                vk::ImageLayout Layout = vk::ImageLayout::eGeneral;
                vk::PipelineStageFlags2 Stages = vk::PipelineStageFlagBits2::eNone;
                vk::AccessFlags2 Access = vk::AccessFlagBits2::eNone;
                bool IsWrite = false;
            };

            std::unordered_map<ImageSubresource, MergedImageUsage, ImageSubresourceHash> imageUsages;
            for (const ImageUsage& usage : pass.ImageUsages)
            {
                const auto& imageRange = m_Images[usage.Image.Index].Range;
                const uint32_t mipCount = usage.SubresourceRange.levelCount == vk::RemainingMipLevels
                    ? imageRange.levelCount
                    : usage.SubresourceRange.levelCount;
                const uint32_t layerCount = usage.SubresourceRange.layerCount == vk::RemainingArrayLayers
                    ? imageRange.layerCount
                    : usage.SubresourceRange.layerCount;
                if (mipCount == vk::RemainingMipLevels || layerCount == vk::RemainingArrayLayers)
                    throw std::logic_error("Render graph image range has unresolved subresource count");

                for (uint32_t mip = usage.SubresourceRange.baseMipLevel;
                     mip < usage.SubresourceRange.baseMipLevel + mipCount; ++mip)
                {
                    for (uint32_t layer = usage.SubresourceRange.baseArrayLayer;
                         layer < usage.SubresourceRange.baseArrayLayer + layerCount; ++layer)
                    {
                        const ImageSubresource key{ usage.Image.Index, mip, layer };
                        auto [it, inserted] = imageUsages.try_emplace(key, MergedImageUsage{
                            .AspectMask = usage.SubresourceRange.aspectMask,
                            .Layout = usage.Layout,
                            .Stages = usage.Stages,
                            .Access = usage.Access,
                            .IsWrite = usage.Usage == ResourceUsage::Write });
                        if (!inserted)
                        {
                            if (it->second.Layout != usage.Layout)
                                throw std::logic_error("Conflicting image layouts for overlapping subresources in render pass");
                            it->second.AspectMask |= usage.SubresourceRange.aspectMask;
                            it->second.Stages |= usage.Stages;
                            it->second.Access |= usage.Access;
                            it->second.IsWrite |= usage.Usage == ResourceUsage::Write;
                        }
                    }
                }
            }

            for (const auto& [key, usage] : imageUsages)
            {
                auto [stateIt, inserted] = imgStates.try_emplace(key, ImgState{
                    .Current = m_Images[key.Image].InitialState,
                    .LastWrite = m_Images[key.Image].InitialState
                });
                ImgState& previous = stateIt->second;
                const ImageState old = previous.Current;
                const vk::PipelineStageFlags2 sourceStages =
                    old.Stages | (previous.HasWrite ? previous.LastWrite.Stages : vk::PipelineStageFlagBits2::eNone);
                const vk::AccessFlags2 sourceAccess =
                    old.Access | (previous.HasWrite ? previous.LastWrite.Access : vk::AccessFlagBits2::eNone);

                if (!previous.HasUse || previous.WasWrite || usage.IsWrite || old.Layout != usage.Layout ||
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
                                                       .image = m_Images[key.Image].Image,
                                                       .subresourceRange = { .aspectMask = usage.AspectMask,
                                                                             .baseMipLevel = key.Mip,
                                                                             .levelCount = 1,
                                                                             .baseArrayLayer = key.Layer,
                                                                             .layerCount = 1 } });
                }
                previous.Current = { .Layout = usage.Layout, .Stages = usage.Stages, .Access = usage.Access };
                previous.HasUse = true;
                if (usage.IsWrite)
                {
                    previous.LastWrite = previous.Current;
                    previous.HasWrite = true;
                }
                previous.WasWrite = usage.IsWrite;
            }

            // Buffer Barriers
            for (const BufferUsage& usage : pass.BufferUsages) 
            {
                BufState& previous = bufStates[usage.Buffer.Index];
                const BufferState old = previous.Current;
                const bool isWrite = usage.Usage == ResourceUsage::Write;
                const vk::PipelineStageFlags2 sourceStages =
                    old.Stages | (previous.HasWrite ? previous.LastWrite.Stages : vk::PipelineStageFlagBits2::eNone);
                const vk::AccessFlags2 sourceAccess =
                    old.Access | (previous.HasWrite ? previous.LastWrite.Access : vk::AccessFlagBits2::eNone);

                if (!previous.HasUse || previous.WasWrite || isWrite || old.Stages != usage.Stages ||
                    old.Access != usage.Access) 
                {
                    compiled.BufferBarriers.push_back({ .srcStageMask = sourceStages,
                                                        .srcAccessMask = sourceAccess,
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
                if (isWrite)
                {
                    previous.LastWrite = previous.Current;
                    previous.HasWrite = true;
                }
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
