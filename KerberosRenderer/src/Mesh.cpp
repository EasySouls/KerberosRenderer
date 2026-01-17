#include "Mesh.hpp"

#include "Buffer.hpp"
#include "Utils.hpp"

namespace kbr
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
		: m_Vertices(vertices), m_Indices(indices), m_VertexBuffer(vertices), m_IndexBuffer(indices)
	{
	}

	void Mesh::Draw(const vk::CommandBuffer commandBuffer) const 
	{
		commandBuffer.bindVertexBuffers(0, *m_VertexBuffer.GetBuffer(), { 0 });
		commandBuffer.bindIndexBuffer(m_IndexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
		commandBuffer.drawIndexed(static_cast<uint32_t>(m_Indices.size()), 1, 0, 0, 0);
	}
}
