#include "Graph.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>

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
	}

	Graph::PassBuilder& Graph::PassBuilder::Read(const ImageHandle image, const vk::ImageLayout layout,
		const vk::PipelineStageFlags2 stages, const vk::AccessFlags2 access, const vk::ImageSubresourceRange& range)
	{
		m_Graph.AddUsage(m_Pass, { .Image = image, .Usage = ResourceUsage::Read, .Layout = layout, .Stages = stages, .Access = access, .SubresourceRange = range });
		return *this;
	}

	Graph::PassBuilder& Graph::PassBuilder::Write(const ImageHandle image, const vk::ImageLayout layout,
		const vk::PipelineStageFlags2 stages, const vk::AccessFlags2 access, const vk::ImageSubresourceRange& range)
	{
		m_Graph.AddUsage(m_Pass, { .Image = image, .Usage = ResourceUsage::Write, .Layout = layout, .Stages = stages, .Access = access, .SubresourceRange = range });
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

	Graph::PassBuilder Graph::AddPass(const std::string_view name)
	{
		m_Passes.push_back({ .Name = std::string(name), .Usages = {} });
		return { *this, { static_cast<uint32_t>(m_Passes.size() - 1) } };
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

		m_Passes[pass.Index].Usages.push_back(usage);
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
				for (const ImageUsage& left : m_Passes[a].Usages)
					for (const ImageUsage& right : m_Passes[b].Usages)
						if (left.Image == right.Image && (IsWrite(left) || IsWrite(right)))
							dependency = true;
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

		struct State
		{
			ImageState Current{};
			bool HasUse = false;
			bool WasWrite = false;
		};
		std::vector<State> states;
		states.reserve(m_Images.size());
		for (const ImageResource& image : m_Images)
			states.push_back({ .Current = image.InitialState, .HasUse = false, .WasWrite = false });

		m_CompiledPasses.clear();
		for (PassHandle handle : m_ExecutionOrder)
		{
			const Pass& pass = m_Passes[handle.Index];
			CompiledPass compiled{ .Handle = handle, .Name = pass.Name, .ImageBarriers = {} };
			for (const ImageUsage& usage : pass.Usages)
			{
				State& previous = states[usage.Image.Index];
				const ImageState old = previous.Current;
				const bool needsBarrier = !previous.HasUse || previous.WasWrite || IsWrite(usage) ||
					old.Layout != usage.Layout || old.Stages != usage.Stages || old.Access != usage.Access;
				if (needsBarrier)
				{
					vk::ImageMemoryBarrier2 barrier{};
					barrier.srcStageMask = old.Stages;
					barrier.srcAccessMask = old.Access;
					barrier.dstStageMask = usage.Stages;
					barrier.dstAccessMask = usage.Access;
					barrier.oldLayout = old.Layout;
					barrier.newLayout = usage.Layout;
					barrier.image = m_Images[usage.Image.Index].Image;
					barrier.subresourceRange = usage.SubresourceRange;
					compiled.ImageBarriers.push_back(barrier);
				}
				previous.Current = { .Layout = usage.Layout, .Stages = usage.Stages, .Access = usage.Access };
				previous.HasUse = true;
				previous.WasWrite = IsWrite(usage);
			}
			m_CompiledPasses.push_back(std::move(compiled));
		}
		return m_CompiledPasses;
	}

	void Graph::EmitBarriers(const vk::raii::CommandBuffer& commandBuffer, PassHandle pass) const
	{
		const auto found = std::ranges::find_if(m_CompiledPasses,
                                          [pass](const CompiledPass& compiled) { return compiled.Handle.Index == pass.Index; });
		if (found == m_CompiledPasses.end())
		{
			throw std::out_of_range("Render graph pass has not been compiled");
		}
		if (found->ImageBarriers.empty())
		{
			return;
		}

		vk::DependencyInfo dependency{};
		dependency.imageMemoryBarrierCount = static_cast<uint32_t>(found->ImageBarriers.size());
		dependency.pImageMemoryBarriers = found->ImageBarriers.data();
		commandBuffer.pipelineBarrier2(dependency);
	}

	void Graph::Clear()
	{
		m_Images.clear();
		m_Passes.clear();
		m_ExecutionOrder.clear();
		m_CompiledPasses.clear();
	}
}
