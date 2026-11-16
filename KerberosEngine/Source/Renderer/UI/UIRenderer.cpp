#include "kbrpch.hpp"
#include "UIRenderer.hpp"

#include "Buffer.hpp"
#include "Renderer/GraphicsPipeline.hpp"
#include "Renderer/Shaders/Shader.hpp"
#include "Renderer/TextureManager.hpp"
#include "VulkanContext.hpp"

import Kerberos;

namespace Kerberos {

namespace {

constexpr vk::DeviceSize InitialVertexBufferSize = 1024 * 1024; // 16384 UI vertices
constexpr vk::DeviceSize InitialIndexBufferSize = 256 * 1024;   // 65536 indices

constexpr vk::MemoryPropertyFlags HostVisibleMemory = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

}

void UIRenderer::CreateFrameBuffers(FrameBuffer& buffer, const vk::DeviceSize size, const vk::BufferUsageFlags usage)
{
	auto& context = VulkanContext::Get();
	const auto& device = context.GetDevice();

	buffer.Buffer = nullptr;
	buffer.Memory = nullptr;
	buffer.Mapped = nullptr;

	::CreateBuffer(device, size, usage, HostVisibleMemory, buffer.Buffer, buffer.Memory);
	buffer.Mapped = buffer.Memory.mapMemory(0, size);
	buffer.Capacity = size;
}

void UIRenderer::EnsureCapacity(FrameBuffer& buffer, 
                                const vk::DeviceSize requiredSize,
                                const vk::BufferUsageFlags usage)
{
	if (buffer.Capacity >= requiredSize && buffer.Buffer != nullptr)
		return;

	// Buffers can be referenced by in-flight frames, so grow only when the GPU is idle.
	VulkanContext::Get().WaitIdle();

	if (buffer.Memory != nullptr && buffer.Mapped != nullptr)
	{
		buffer.Memory.unmapMemory();
		buffer.Mapped = nullptr;
	}

	vk::DeviceSize newSize = buffer.Capacity > 0 ? buffer.Capacity : InitialVertexBufferSize;
	while (newSize < requiredSize)
		newSize *= 2;

	CreateFrameBuffers(buffer, newSize, usage);
}

void UIRenderer::Init(TextureManager& textureManager, const vk::Format colorFormat)
{
	KBR_TRACY_FUNCTION();

	m_TextureManager = &textureManager;

	auto& context = VulkanContext::Get();
	const auto& device = context.GetDevice();

       constexpr vk::PushConstantRange pushConstantRange{
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		.offset = 0,
		.size = sizeof(UIPushConstants)
	};

	const std::array setLayouts = { textureManager.GetGlobalDescriptorSetLayout() };
	const vk::PipelineLayoutCreateInfo layoutInfo{
		.setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
		.pSetLayouts = setLayouts.data(),
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};

	m_PipelineLayout = vk::raii::PipelineLayout{ device, layoutInfo };
	context.SetObjectDebugName(m_PipelineLayout, "Game UI Pipeline Layout");

	const Ref<Shader> shader = CreateRef<Shader>("ui", "Game UI");
    
    const auto inputAttrDescs = UIVertex::GetAttributeDescriptions();

	GraphicsPipelineSpecification spec{};
	spec.Name = "Game UI Pipeline";
	spec.Shader = shader;
	spec.PipelineLayout = *m_PipelineLayout;
	spec.BindingDescription = UIVertex::GetBindingDescription();
	spec.InputAttributeDescriptions = { inputAttrDescs.begin(), inputAttrDescs.end() };
	spec.SampleCount = vk::SampleCountFlagBits::e1;
	spec.PolygonModeFill = true;
	spec.CullMode = CullMode::None;
	spec.EnableDepthTest = false;
	spec.EnableDepthWrite = false;
	spec.Topology = PrimitiveTopology::TriangleList;
	spec.BlendModes = { BlendMode::AlphaBlend };
	spec.ColorAttachmentFormats = { colorFormat };
	spec.DepthAttachmentFormat = std::nullopt;
	spec.DynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	spec.UsingDescriptorBuffers = false;

	m_Pipeline = CreateRef<GraphicsPipeline>(std::move(spec));

	for (size_t i = 0; i < m_VertexBuffers.size(); ++i)
	{
		CreateFrameBuffers(m_VertexBuffers[i], InitialVertexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
		CreateFrameBuffers(m_IndexBuffers[i], InitialIndexBufferSize, vk::BufferUsageFlagBits::eIndexBuffer);
	}
}

void UIRenderer::Shutdown()
{
	KBR_TRACY_FUNCTION();

	for (auto* buffers : { &m_VertexBuffers, &m_IndexBuffers })
	{
		for (auto& [Buffer, Memory, Mapped, Capacity] : *buffers)
		{
			if (Memory != nullptr && Mapped != nullptr)
			{
				Memory.unmapMemory();
				Mapped = nullptr;
			}
			Buffer = nullptr;
			Memory = nullptr;
			Capacity = 0;
		}
	}

	m_Pipeline = nullptr;
	m_PipelineLayout = nullptr;
	m_TextureManager = nullptr;
}

void UIRenderer::Render(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex,
                        const UIDrawData& drawData, const glm::vec2 screenSize)
{
	KBR_TRACY_FUNCTION();

	if (drawData.Empty() || !m_Pipeline || !m_TextureManager || frameIndex >= m_VertexBuffers.size())
		return;

	const vk::DeviceSize vertexBytes = drawData.Vertices.size() * sizeof(UIVertex);
	const vk::DeviceSize indexBytes = drawData.Indices.size() * sizeof(uint32_t);

	FrameBuffer& vertexBuffer = m_VertexBuffers[frameIndex];
	FrameBuffer& indexBuffer = m_IndexBuffers[frameIndex];

	EnsureCapacity(vertexBuffer, vertexBytes, vk::BufferUsageFlagBits::eVertexBuffer);
	EnsureCapacity(indexBuffer, indexBytes, vk::BufferUsageFlagBits::eIndexBuffer);

	if (!vertexBuffer.Mapped || !indexBuffer.Mapped)
		return;

	std::memcpy(vertexBuffer.Mapped, drawData.Vertices.data(), static_cast<size_t>(vertexBytes));
	std::memcpy(indexBuffer.Mapped, drawData.Indices.data(), static_cast<size_t>(indexBytes));


	cmd.setViewport(0, vk::Viewport{ .x = 0.0f, .y = 0.0f, .width = screenSize.x, .height = screenSize.y, .minDepth = 0.0f, .maxDepth = 1.0f });
	cmd.setScissor(0, vk::Rect2D{ .offset = { .x = 0, .y = 0 },
                                     .extent = { .width = static_cast<uint32_t>(screenSize.x), .height = static_cast<uint32_t>(screenSize.y) } });

	m_Pipeline->Bind(cmd);

	cmd.bindVertexBuffers(0, { *vertexBuffer.Buffer }, { 0 });
	cmd.bindIndexBuffer(*indexBuffer.Buffer, 0, vk::IndexType::eUint32);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_PipelineLayout, 0,
	                       { m_TextureManager->GetGlobalDescriptorSet() }, {});

	const UIPushConstants pushConstants{ .ScreenSize = screenSize };
	cmd.pushConstants<UIPushConstants>(*m_PipelineLayout,
	                                   vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
	                                   0, pushConstants);

	cmd.drawIndexed(static_cast<uint32_t>(drawData.Indices.size()), 1, 0, 0, 0);
}

}
