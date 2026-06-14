#include "kbrpch.hpp"
#include "MeshImporter.hpp"

#include <fstream>

namespace Kerberos
{
	Ref<Mesh> MeshImporter::ImportMesh(const AssetHandle /*handle*/, const AssetMetadata& metadata)
	{
		return ImportMesh(metadata.Filepath);
	}

	Ref<Mesh> MeshImporter::ImportMesh(const std::filesystem::path& filepath)
	{
		std::ifstream in(filepath, std::ios::binary);
		if (!in.is_open())
		{
			KBR_CORE_ERROR("MeshImporter: failed to open mesh file {}", filepath.string());
			return nullptr;
		}

		uint32_t version = 0;
		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;
		in.read(reinterpret_cast<char*>(&version), sizeof(version));
		in.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
		in.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));

		std::vector<Vertex> vertices;
		vertices.resize(vertexCount);
		for (uint32_t i = 0; i < vertexCount; ++i)
		{
			Vertex v;
			in.read(reinterpret_cast<char*>(&v.Position), sizeof(glm::vec3));
			in.read(reinterpret_cast<char*>(&v.Normal), sizeof(glm::vec3));
			in.read(reinterpret_cast<char*>(&v.Tangent), sizeof(glm::vec4));
			in.read(reinterpret_cast<char*>(&v.TexCoord), sizeof(glm::vec2));
			vertices[i] = v;
		}

		std::vector<uint32_t> indices;
		indices.resize(indexCount);
		in.read(reinterpret_cast<char*>(indices.data()), sizeof(uint32_t) * indexCount);

		in.close();

		// Use filename as mesh name
		const std::string name = filepath.stem().string();
		Ref<Mesh> mesh = CreateRef<Mesh>(name, vertices, indices);
		return mesh;
	}
}
