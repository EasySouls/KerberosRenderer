#pragma once

#include "Vulkan.hpp"
#include "Vertex.hpp"
#include "Buffer.hpp"

#include <vector>

namespace kbr
{
	class Mesh
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		virtual ~Mesh() = default;

		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;

		Mesh(Mesh&&) = default;
		Mesh& operator=(Mesh&&) = default;

		void Draw(vk::CommandBuffer commandBuffer) const;

	private:
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;

		VertexBuffer m_VertexBuffer;
		IndexBuffer m_IndexBuffer;
	};
}
