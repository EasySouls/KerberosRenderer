#include "RuntimeAssetLoader.hpp"

#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Importers/MaterialImporter.hpp"
#include "Assets/Model.hpp"
#include "Assets/Prefab.hpp"
#include "Serialization/PrefabSerializer.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Vertex.hpp"
#include "Profiling/Profilers.hpp"

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
	KBR_TRACY_FUNCTION();

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
	KBR_TRACY_FUNCTION();

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

AssetHandle FindSubasset(const AssetRegistry* registry, const AssetHandle rootHandle,
	const std::string& key)
{
	if (!registry)
		return AssetHandle::Invalid();

	for (const auto& [handle, metadata] : *registry)
	{
		if (metadata.RootSourceHandle == rootHandle && metadata.SubAssetKey == key)
			return handle;
	}
	return AssetHandle::Invalid();
}

Ref<Prefab> LoadNativePrefab(const AssetHandle handle, const AssetMetadata& metadata,
	const std::filesystem::path& path, const AssetRegistry* registry)
{
	NativeAssetRecord record;
	if (!NativeAssetSerializer::DeserializeRecord(path, record) || record.Kind != "prefab")
		return nullptr;

	GltfSceneManifest manifest;
	const auto manifestPath = path.parent_path() / (path.stem().string() + ".kbrscene");
	if (!NativeAssetSerializer::DeserializeSceneManifest(manifestPath, manifest))
		return nullptr;

	auto prefab = CreateRef<Prefab>();
	prefab->Name = path.stem().string();
	prefab->RootLocalIndex = InvalidPrefabLocalIndex;
	const AssetHandle rootHandle = metadata.RootSourceHandle.IsValid()
		? metadata.RootSourceHandle : handle;
	prefab->Entities.reserve(manifest.Nodes.size());

	for (size_t i = 0; i < manifest.Nodes.size(); ++i)
	{
		const auto& node = manifest.Nodes[i];
		PrefabEntityData entity;
		entity.LocalIndex = static_cast<PrefabLocalIndex>(i);
		entity.Name = node.Name;
		entity.Tag = node.Name;
		entity.ParentLocalIndex = node.ParentIndex >= 0
			? static_cast<PrefabLocalIndex>(node.ParentIndex)
			: InvalidPrefabLocalIndex;

		if (node.ParentIndex < 0 && prefab->RootLocalIndex == InvalidPrefabLocalIndex)
			prefab->RootLocalIndex = entity.LocalIndex;

		if (node.MeshIndex >= 0)
		{
			const std::string meshKey = "mesh:" + std::to_string(node.MeshIndex) + "_0";
			const AssetHandle meshHandle = FindSubasset(registry, rootHandle, meshKey);
			if (meshHandle.IsValid())
			{
				entity.StaticMeshAsset = meshHandle;
				entity.HasStaticMesh = true;
			}
		}
		prefab->Entities.push_back(std::move(entity));
	}

	for (auto& entity : prefab->Entities)
	{
		if (entity.ParentLocalIndex != InvalidPrefabLocalIndex
			&& entity.ParentLocalIndex < prefab->Entities.size())
			prefab->Entities[entity.ParentLocalIndex].Children.push_back(entity.LocalIndex);
	}

	if (prefab->RootLocalIndex == InvalidPrefabLocalIndex && !prefab->Entities.empty())
		prefab->RootLocalIndex = 0;
	prefab->GetHandle() = handle;
	return prefab;
}

Ref<Texture2D> LoadTexture(const AssetHandle handle, const std::filesystem::path& path)
{
	auto texture = Texture2D::FromFile(path);
	if (texture)
		texture->GetHandle() = handle;
	return texture;
}

Ref<Material> LoadMaterial(const std::filesystem::path& path)
{
	// Compatibility reader for authored materials until native material artifacts
	// are emitted by the material pipeline.
	return MaterialImporter::ImportMaterial(path);
}

}

Ref<Asset> RuntimeAssetLoader::Load(const AssetHandle handle,
	const AssetMetadata& metadata, const std::filesystem::path& path,
	const AssetRegistry* registry)
{
	KBR_TRACY_FUNCTION();

	switch (metadata.Type)
	{
		case AssetType::Texture2D:
			return LoadTexture(handle, path);
		case AssetType::Material:
			return LoadMaterial(path);
		case AssetType::Mesh:
			return LoadMesh(handle, path);
		case AssetType::ModelDEPRECATED:
			return LoadSceneManifest(handle, path);
		case AssetType::Prefab:
		{
			NativeAssetRecord record;
			if (NativeAssetSerializer::DeserializeRecord(path, record))
				return record.Kind == "prefab"
					? LoadNativePrefab(handle, metadata, path, registry)
					: nullptr;

			// Authored YAML prefabs remain readable; generated native cache
			// prefabs are always recognized by the native record reader above.
			auto prefab = PrefabSerializer::DeserializePrefab(path);
			if (prefab)
				prefab->GetHandle() = handle;
			return prefab;
		}
		default:
            KBRAssert("Runtime asset type {} has no native loader: {}",
                      AssetTypeToString(metadata.Type),
                      path.string());
			Log::CoreError("Runtime asset type {} has no native loader: {}", AssetTypeToString(metadata.Type), path.string());
			return nullptr;
	}
}

}