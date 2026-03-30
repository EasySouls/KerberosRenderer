#pragma once

#include "Vulkan.hpp"
#include "Renderer/Vertex.hpp"
#include "Assets/Asset.hpp"
#include "Buffer.hpp"

#include <vector>
#include <string>


namespace Kerberos
{
	class Mesh : public Asset
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		Mesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		~Mesh() override = default;

		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;

		Mesh(Mesh&&) = default;
		Mesh& operator=(Mesh&&) = default;

		void Draw(vk::CommandBuffer commandBuffer) const;

		void SetDebugName(const std::string& name) const;

		AssetType GetType() override { return AssetType::Mesh; }

		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

		const VertexBuffer& GetVertexBuffer() const { return m_VertexBuffer; }
		const IndexBuffer& GetIndexBuffer() const { return m_IndexBuffer; }

	private:
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;

		std::string m_Name;

		VertexBuffer m_VertexBuffer;
		IndexBuffer m_IndexBuffer;
	};
}
