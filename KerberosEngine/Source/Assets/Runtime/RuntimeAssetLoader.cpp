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
#include <sstream>
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
	prefab->RequiresInstanceRoot = true;
	const AssetHandle rootHandle = metadata.RootSourceHandle.IsValid()
		? metadata.RootSourceHandle : handle;
	std::vector<PrefabLocalIndex> nodeLocalIndices;
	nodeLocalIndices.reserve(manifest.Nodes.size());
	prefab->Entities.reserve(manifest.Nodes.size() * 2);

	for (size_t i = 0; i < manifest.Nodes.size(); ++i)
	{
		const auto& node = manifest.Nodes[i];
		PrefabEntityData entity;
		entity.LocalIndex = static_cast<PrefabLocalIndex>(prefab->Entities.size());
		entity.Name = node.Name;
		entity.Tag = node.Name;
		nodeLocalIndices.push_back(entity.LocalIndex);
		prefab->Entities.push_back(std::move(entity));
	}

	for (size_t i = 0; i < manifest.Nodes.size(); ++i)
	{
		const auto& node = manifest.Nodes[i];
		auto& nodeEntity = prefab->Entities[nodeLocalIndices[i]];
		nodeEntity.ParentLocalIndex = node.ParentIndex >= 0
			&& static_cast<size_t>(node.ParentIndex) < nodeLocalIndices.size()
			? nodeLocalIndices[static_cast<size_t>(node.ParentIndex)]
			: InvalidPrefabLocalIndex;
		if (nodeEntity.ParentLocalIndex == InvalidPrefabLocalIndex
			&& prefab->RootLocalIndex == InvalidPrefabLocalIndex)
			prefab->RootLocalIndex = nodeEntity.LocalIndex;

		for (const auto& primitive : node.Primitives)
		{
			PrefabEntityData primitiveEntity;
			primitiveEntity.LocalIndex = static_cast<PrefabLocalIndex>(prefab->Entities.size());
			primitiveEntity.Name = node.Name + "_Primitive_" + std::to_string(primitive.PrimitiveIndex);
			primitiveEntity.Tag = primitiveEntity.Name;
			primitiveEntity.ParentLocalIndex = nodeEntity.LocalIndex;

			const std::string meshKey = "mesh:" + std::to_string(primitive.MeshIndex)
				+ "_" + std::to_string(primitive.PrimitiveIndex);
			const AssetHandle meshHandle = FindSubasset(registry, rootHandle, meshKey);
			if (meshHandle.IsValid())
			{
				primitiveEntity.StaticMeshAsset = meshHandle;
				primitiveEntity.HasStaticMesh = true;
			}
			else
			{
				Log::CoreError("Native prefab {} is missing mesh subasset {}", path.string(), meshKey);
			}

			if (primitive.MaterialIndex >= 0)
			{
				const std::string materialKey = "material:" + std::to_string(primitive.MaterialIndex);
				primitiveEntity.StaticMaterialAsset = FindSubasset(registry, rootHandle, materialKey);
				if (!primitiveEntity.StaticMaterialAsset.IsValid())
					Log::CoreError("Native prefab {} is missing material subasset {}", path.string(), materialKey);
			}
			prefab->Entities.push_back(std::move(primitiveEntity));
		}
	}

	for (size_t i = 0; i < prefab->Entities.size(); ++i)
	{
		auto& entity = prefab->Entities[i];
		if (entity.ParentLocalIndex != InvalidPrefabLocalIndex && entity.ParentLocalIndex < prefab->Entities.size())
		{
			prefab->Entities[entity.ParentLocalIndex].Children.push_back(entity.LocalIndex);
		}
	}

	if (prefab->RootLocalIndex == InvalidPrefabLocalIndex && !prefab->Entities.empty())
	{
		prefab->RootLocalIndex = 0;
	}
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

Ref<Material> LoadNativeMaterial(AssetHandle handle, const std::filesystem::path& path,
	const AssetRegistry* registry, const AssetHandle rootHandle, const std::filesystem::path& libraryRoot)
{
	NativeAssetRecord record;
	if (!NativeAssetSerializer::DeserializeRecord(path, record) || record.Kind != "material")
		return nullptr;

	auto material = CreateRef<Material>();
	material->GetHandle() = handle;
	material->Name = path.stem().string();

	int baseColorTexture = -1;
	int normalTexture = -1;
	int metallicRoughnessTexture = -1;
	int occlusionTexture = -1;
	int emissiveTexture = -1;

	std::istringstream stream(record.Data);
	std::string field;
	while (std::getline(stream, field, ';'))
	{
		const auto separator = field.find('=');
		if (separator == std::string::npos)
			continue;
		const std::string key = field.substr(0, separator);
		const std::string value = field.substr(separator + 1);
		if (key == "name")
			material->Name = value;
		else if (key == "metallic")
			material->Params.MetallicFactor = std::stof(value);
		else if (key == "roughness")
			material->Params.RoughnessFactor = std::stof(value);
		else if (key == "baseColor")
		{
			std::istringstream colors(value);
			std::string component;
			float* destination = &material->Params.AlbedoFactor.x;
			for (size_t i = 0; i < 4 && std::getline(colors, component, ','); ++i)
				destination[i] = std::stof(component);
		}
		else if (key == "emissive")
		{
			std::istringstream colors(value);
			std::string component;
			float* destination = &material->EmissiveColor.x;
			for (size_t i = 0; i < 3 && std::getline(colors, component, ','); ++i)
				destination[i] = std::stof(component);
			material->EmissiveIntensity = 1.0f;
			material->Params.Emissive = material->EmissiveColor;
		}
		else if (key == "baseColorTexture")
			baseColorTexture = std::stoi(value);
		else if (key == "normalTexture")
			normalTexture = std::stoi(value);
		else if (key == "metallicRoughnessTexture")
			metallicRoughnessTexture = std::stoi(value);
		else if (key == "occlusionTexture")
			occlusionTexture = std::stoi(value);
		else if (key == "emissiveTexture")
			emissiveTexture = std::stoi(value);
	}

	const auto loadTexture = [&](const int imageIndex) -> Ref<Texture2D>
	{
		if (imageIndex < 0)
			return nullptr;

		const AssetHandle textureHandle = FindSubasset(registry, rootHandle, "texture:" + std::to_string(imageIndex));
		if (!textureHandle.IsValid() || !registry)
		{
			Log::CoreWarn("Material '{}' references missing texture sub-asset '{}'", material->Name,
				"texture:" + std::to_string(imageIndex));
			return nullptr;
		}

		const AssetMetadata& textureMeta = registry->Get(textureHandle);
		std::filesystem::path texturePath = textureMeta.LibraryPath;
		if (texturePath.is_relative() && !libraryRoot.empty())
			texturePath = libraryRoot / texturePath;

		Ref<Texture2D> texture = LoadTexture(textureHandle, texturePath);
		if (!texture)
			Log::CoreWarn("Failed to load texture '{}' for material '{}'", texturePath.string(), material->Name);
		return texture;
	};

	material->AlbedoTexture = loadTexture(baseColorTexture);
	material->NormalTexture = loadTexture(normalTexture);
	material->MetallicTexture = loadTexture(metallicRoughnessTexture);
	material->RoughnessTexture = material->MetallicTexture;
	material->AOTexture = loadTexture(occlusionTexture);
	material->EmissiveTexture = loadTexture(emissiveTexture);

	return material;
}

}

Ref<Asset> RuntimeAssetLoader::Load(const AssetHandle handle,
	const AssetMetadata& metadata, const std::filesystem::path& path,
	const AssetRegistry* registry, const std::filesystem::path& libraryRoot)
{
	KBR_TRACY_FUNCTION();

	const AssetHandle rootHandle = metadata.RootSourceHandle.IsValid() ? metadata.RootSourceHandle : handle;

	switch (metadata.Type)
	{
		case AssetType::Texture2D:
			return LoadTexture(handle, path);
		case AssetType::Material:
		{
			NativeAssetRecord record;
			if (NativeAssetSerializer::DeserializeRecord(path, record))
				return LoadNativeMaterial(handle, path, registry, rootHandle, libraryRoot);
			return LoadMaterial(path);
		}
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