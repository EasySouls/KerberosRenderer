#pragma once

#include "Vulkan.hpp"
#include "Renderer/Vertex.hpp"
#include "Buffer.hpp"

#include <vector>
#include <string>

namespace Kerberos
{
	class Mesh
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		Mesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		virtual ~Mesh() = default;

		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;

		Mesh(Mesh&&) = default;
		Mesh& operator=(Mesh&&) = default;

		void Draw(vk::CommandBuffer commandBuffer) const;

		void SetDebugName(const std::string& name) const;

	private:
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;

		std::string m_Name;

		VertexBuffer m_VertexBuffer;
		IndexBuffer m_IndexBuffer;
	};
}
