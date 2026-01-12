#include "Mesh.hpp"

#include "Buffer.hpp"
#include "Utils.hpp"

namespace kbr
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
		: m_Vertices(vertices), m_Indices(indices), m_VertexBuffer(vertices), m_IndexBuffer(indices)
	{
	}
}
