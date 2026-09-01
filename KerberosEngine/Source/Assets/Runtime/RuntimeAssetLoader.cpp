#include "RuntimeAssetLoader.hpp"

#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Model.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Vertex.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

import Kerberos;

namespace Kerberos {

namespace {

constexpr uint32_t MaxVertices = 16 * 1024 * 1024;
constexpr uint32_t MaxIndices = 32 * 1024 * 1024;

template<typename T>
bool Read(std::istream& stream, T& value)
{
	stream.read(reinterpret_cast<char*>(&value), sizeof(T));
	return stream.good();
}

Ref<Mesh> LoadMesh(AssetHandle handle, const std::filesystem::path& path)
{
	NativeMeshPayload native;
	if (NativeAssetSerializer::DeserializeMesh(path, native))
	{
		if (native.VertexStride != sizeof(Vertex) || native.VertexData.size() % sizeof(Vertex) != 0
			|| native.VertexData.size() / sizeof(Vertex) > MaxVertices
			|| native.Indices.size() > MaxIndices)
			return nullptr;
		std::vector<Vertex> vertices(native.VertexData.size() / sizeof(Vertex));
		if (!native.VertexData.empty())
			std::memcpy(vertices.data(), native.VertexData.data(), native.VertexData.size());
		auto mesh = CreateRef<Mesh>(path.stem().string(), vertices, native.Indices);
		mesh->GetHandle() = handle;
		return mesh;
	}

	// Legacy .kbrmesh files contain version, vertex count, index count,
	// followed by the four fields of each Vertex.
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream)
		return nullptr;
	const auto end = stream.tellg();
	if (end < 0 || static_cast<uint64_t>(end) > 512ull * 1024ull * 1024ull)
		return nullptr;
	const uint64_t fileSize = static_cast<uint64_t>(end);
	stream.seekg(0);
	uint32_t version = 0, vertexCount = 0, indexCount = 0;
	if (!Read(stream, version) || !Read(stream, vertexCount) || !Read(stream, indexCount)
		|| version == 0 || vertexCount > MaxVertices || indexCount > MaxIndices)
		return nullptr;
	const uint64_t vertexBytes = static_cast<uint64_t>(vertexCount) *
		(sizeof(glm::vec3) + sizeof(glm::vec3) + sizeof(glm::vec4) + sizeof(glm::vec2));
	const uint64_t indexBytes = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
	if (12ull + vertexBytes + indexBytes != fileSize)
		return nullptr;
	std::vector<Vertex> vertices(vertexCount);
	for (auto& vertex : vertices)
		if (!Read(stream, vertex.Position) || !Read(stream, vertex.Normal)
			|| !Read(stream, vertex.Tangent) || !Read(stream, vertex.TexCoord))
			return nullptr;
	std::vector<uint32_t> indices(indexCount);
	if (indexBytes && !stream.read(reinterpret_cast<char*>(indices.data()), indexBytes))
		return nullptr;
	auto mesh = CreateRef<Mesh>(path.stem().string(), vertices, indices);
	mesh->GetHandle() = handle;
	return mesh;
}

Ref<Model> LoadSceneManifest(AssetHandle handle, const std::filesystem::path& path)
{
	GltfSceneManifest manifest;
	if (!NativeAssetSerializer::DeserializeSceneManifest(path, manifest))
		return nullptr;
	auto model = CreateRef<Model>(path.stem().string());
	model->GetHandle() = handle;
	auto& nodes = model->GetNodes();
	nodes.reserve(manifest.Nodes.size());
	for (const auto& source : manifest.Nodes)
	{
		ModelNode node;
		node.Name = source.Name;
		node.ParentIndex = source.ParentIndex;
		node.SkinIndex = source.SkinIndex;
		nodes.emplace_back(std::move(node));
	}
	return model;
}

}

Ref<Asset> RuntimeAssetLoader::Load(const AssetHandle handle,
	const AssetMetadata& metadata, const std::filesystem::path& path)
{
	switch (metadata.Type)
	{
		case AssetType::Mesh:
			return LoadMesh(handle, path);
		case AssetType::Model:
			return LoadSceneManifest(handle, path);
		default:
			Log::CoreWarn("Runtime asset type {} has no native loader: {}", AssetTypeToString(metadata.Type), path.string());
			return nullptr;
	}
}

}