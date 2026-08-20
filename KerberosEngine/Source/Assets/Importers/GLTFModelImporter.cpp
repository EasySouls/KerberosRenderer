#include "GLTFModelImporter.hpp"
#include "kbrpch.hpp"

#include "Core/UUID.hpp"
#include "Renderer/Vertex.hpp"
#include "TextureImporter.hpp"
#include "Serialization/PrefabSerializer.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Project/Project.hpp"

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>
#include <ktx.h>
#include <tiny_gltf.h>
#include <yaml-cpp/yaml.h>

#include <cstring>
#include <limits>
#include <unordered_map>

namespace Kerberos
{
	namespace
	{
		static bool IsKtxData(const unsigned char* bytes, const int size)
		{
			static constexpr unsigned char ktx1Magic[] = {
				0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
			};
			static constexpr unsigned char ktx2Magic[] = {
				0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
			};

			return bytes && size >= static_cast<int>(sizeof(ktx1Magic))
				&& (std::memcmp(bytes, ktx1Magic, sizeof(ktx1Magic)) == 0
					|| std::memcmp(bytes, ktx2Magic, sizeof(ktx2Magic)) == 0);
		}

		bool LoadImageDataWithKtxFallback(
			tinygltf::Image* image,
			const int imageIdx,
			std::string* err,
			std::string* warn,
			const int reqWidth,
			const int reqHeight,
			const unsigned char* bytes,
			const int size,
			const void* userData)
		{
			(void)userData;

			if (IsKtxData(bytes, size))
			{
				ktxTexture* ktxTexture = nullptr;
				const ktxResult result = ktxTexture_CreateFromMemory(bytes, static_cast<ktx_size_t>(size), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
				if (result != KTX_SUCCESS || !ktxTexture)
				{
					if (err)
						(*err) += "Failed to load KTX image data for image[" + std::to_string(imageIdx) + "].\n";
					return false;
				}

				if ((reqWidth > 0 && static_cast<int>(ktxTexture->baseWidth) != reqWidth)
					|| (reqHeight > 0 && static_cast<int>(ktxTexture->baseHeight) != reqHeight))
				{
					if (warn)
					{
						(*warn) += "KTX image dimensions do not match the glTF image request for image[" + std::to_string(imageIdx) + "].\n";
					}
				}

				image->width = static_cast<int>(ktxTexture->baseWidth);
				image->height = static_cast<int>(ktxTexture->baseHeight);
				image->as_is = true;
				image->image.resize(static_cast<size_t>(size));
				std::memcpy(image->image.data(), bytes, static_cast<size_t>(size));

				ktxTexture_Destroy(ktxTexture);
				return true;
			}

			return tinygltf::LoadImageData(image, imageIdx, err, warn, reqWidth, reqHeight, bytes, size, nullptr);
		}

		static glm::vec3 ReadVec3(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
		{
			KBR_CORE_ASSERT(accessor.type == TINYGLTF_TYPE_VEC3, "Accessor type must be vec3");
			KBR_CORE_ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, "Accessor component type must be float");

			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = model.buffers[view.buffer];

			const size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 3;
			const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset + index * stride;
			const float* f = reinterpret_cast<const float*>(data);
			return { f[0], f[1], f[2] };
		}

		static glm::vec4 ReadVec4(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
		{
			KBR_CORE_ASSERT(accessor.type == TINYGLTF_TYPE_VEC4, "Accessor type must be vec4");
			KBR_CORE_ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, "Accessor component type must be float");

			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = model.buffers[view.buffer];

			const size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 4;
			const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset + index * stride;
			const float* f = reinterpret_cast<const float*>(data);
			return { f[0], f[1], f[2], f[3] };
		}

		static glm::vec2 ReadVec2(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
		{
			KBR_CORE_ASSERT(accessor.type == TINYGLTF_TYPE_VEC2, "Accessor type must be vec2");
			KBR_CORE_ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, "Accessor component type must be float");

			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = model.buffers[view.buffer];

			const size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 2;
			const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset + index * stride;
			const float* f = reinterpret_cast<const float*>(data);
			return { f[0], f[1] };
		}

		static void GenerateTangentsForVertices(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
		{
			if (vertices.empty() || indices.size() < 3)
				return;

			std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
			std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				const uint32_t i0 = indices[i];
				const uint32_t i1 = indices[i + 1];
				const uint32_t i2 = indices[i + 2];
				if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
					continue;

				const Vertex& v0 = vertices[i0];
				const Vertex& v1 = vertices[i1];
				const Vertex& v2 = vertices[i2];

				const glm::vec3 edge1 = v1.Position - v0.Position;
				const glm::vec3 edge2 = v2.Position - v0.Position;
				const glm::vec2 deltaUV1 = v1.TexCoord - v0.TexCoord;
				const glm::vec2 deltaUV2 = v2.TexCoord - v0.TexCoord;

				const float det = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
				if (glm::abs(det) <= std::numeric_limits<float>::epsilon())
					continue;

				const float invDet = 1.0f / det;

				glm::vec3 tangent{};
				tangent.x = invDet * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
				tangent.y = invDet * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
				tangent.z = invDet * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

				glm::vec3 bitangent{};
				bitangent.x = invDet * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
				bitangent.y = invDet * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
				bitangent.z = invDet * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

				tan1[i0] += tangent;
				tan1[i1] += tangent;
				tan1[i2] += tangent;

				tan2[i0] += bitangent;
				tan2[i1] += bitangent;
				tan2[i2] += bitangent;
			}

			for (size_t i = 0; i < vertices.size(); ++i)
			{
				const glm::vec3 n = glm::normalize(vertices[i].Normal);
				const glm::vec3 t = tan1[i];
				glm::vec3 orthogonalizedTangent = t - n * glm::dot(n, t);

				if (glm::length2(orthogonalizedTangent) <= std::numeric_limits<float>::epsilon())
				{
					const glm::vec3 c1 = glm::cross(n, glm::vec3(0.0f, 0.0f, 1.0f));
					const glm::vec3 c2 = glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f));
					orthogonalizedTangent = glm::length2(c1) > glm::length2(c2) ? c1 : c2;
				}

				orthogonalizedTangent = glm::normalize(orthogonalizedTangent);
				const float handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
				vertices[i].Tangent = glm::vec4(orthogonalizedTangent, handedness);
			}
		}

		static std::vector<uint32_t> ReadIndices(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
		{
			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = model.buffers[view.buffer];
			const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;

			std::vector<uint32_t> indices;
			indices.reserve(accessor.count);

			switch (accessor.componentType)
			{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				{
					const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
					for (size_t i = 0; i < accessor.count; ++i)
						indices.push_back(src[i]);
					break;
				}
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				{
					const uint16_t* src = reinterpret_cast<const uint16_t*>(data);
					for (size_t i = 0; i < accessor.count; ++i)
						indices.push_back(src[i]);
					break;
				}
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				{
					const uint32_t* src = reinterpret_cast<const uint32_t*>(data);
					for (size_t i = 0; i < accessor.count; ++i)
						indices.push_back(src[i]);
					break;
				}
				default:
					throw std::runtime_error("Unsupported glTF index component type");
			}

			return indices;
		}

		static std::filesystem::path ResolveTexturePath(const std::filesystem::path& modelPath, const std::string& uri)
		{
			if (uri.empty())
				return {};

			std::filesystem::path texturePath(uri);
			if (texturePath.is_relative())
				texturePath = modelPath.parent_path() / texturePath;

			return texturePath.lexically_normal();
		}

		static Ref<Texture2D> LoadTextureFromTextureInfo(
			const tinygltf::Model& gltfModel,
			const int textureIndex,
			const std::filesystem::path& modelPath,
			std::unordered_map<std::string, Ref<Texture2D>>& textureCache)
		{
			if (textureIndex < 0 || textureIndex >= static_cast<int>(gltfModel.textures.size()))
				return nullptr;

			const tinygltf::Texture& texture = gltfModel.textures[textureIndex];
			if (texture.source < 0 || texture.source >= static_cast<int>(gltfModel.images.size()))
				return nullptr;

			const tinygltf::Image& image = gltfModel.images[texture.source];
			if (image.uri.empty())
			{
				KBR_CORE_WARN("Embedded glTF image detected for texture index {}. File-backed import only is currently supported.", textureIndex);
				return nullptr;
			}

			const std::filesystem::path texturePath = ResolveTexturePath(modelPath, image.uri);
			const std::string cacheKey = texturePath.string();
			if (textureCache.contains(cacheKey))
				return textureCache[cacheKey];

			const Ref<Texture2D> loadedTexture = TextureImporter::ImportTexture(texturePath);
			textureCache.emplace(cacheKey, loadedTexture);
			return loadedTexture;
		}

		static ModelAnimationPath ToAnimationPath(const std::string& path)
		{
			if (path == "translation")
				return ModelAnimationPath::Translation;
			if (path == "rotation")
				return ModelAnimationPath::Rotation;
			if (path == "scale")
				return ModelAnimationPath::Scale;
			return ModelAnimationPath::Weights;
		}
	}

	Ref<Model> GLTFModelImporter::ImportModel(AssetHandle, const AssetMetadata& metadata)
	{
		return ImportModel(metadata.Filepath);
	}

	Ref<Model> GLTFModelImporter::ImportModel(const std::filesystem::path& filepath)
	{
		tinygltf::Model gltfModel;
		tinygltf::TinyGLTF loader;
		loader.SetImageLoader(LoadImageDataWithKtxFallback, nullptr);
		std::string err;
		std::string warn;

		bool ok = false;
		if (filepath.extension() == ".glb")
			ok = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filepath.string());
		else if (filepath.extension() == ".gltf")
			ok = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filepath.string());
		else
			throw std::runtime_error("Unsupported glTF extension: " + filepath.extension().string());

		if (!warn.empty())
			KBR_CORE_WARN("glTF loader warning: {}", warn);
		if (!err.empty())
			KBR_CORE_ERROR("glTF loader error: {}", err);
		if (!ok)
			throw std::runtime_error("Failed to import glTF file: " + filepath.string());

		const Ref<Model> modelAsset = CreateRef<Model>(filepath.stem().string());
		auto& outPrimitives = modelAsset->GetPrimitives();
		auto& outMaterials = modelAsset->GetMaterials();
		auto& outNodes = modelAsset->GetNodes();
		auto& outSkins = modelAsset->GetSkins();
		auto& outAnimations = modelAsset->GetAnimations();

		std::unordered_map<std::string, Ref<Texture2D>> textureCache;
		outMaterials.reserve(gltfModel.materials.size());
		for (size_t materialIndex = 0; materialIndex < gltfModel.materials.size(); ++materialIndex)
		{
			const tinygltf::Material& sourceMaterial = gltfModel.materials[materialIndex];
			Ref<Material> material = CreateRef<Material>();
			material->Name = sourceMaterial.name.empty()
				? "Material_" + std::to_string(materialIndex)
				: sourceMaterial.name;

			if (sourceMaterial.pbrMetallicRoughness.baseColorFactor.size() == 4)
			{
				material->Params.AlbedoFactor = glm::vec4(
					static_cast<float>(sourceMaterial.pbrMetallicRoughness.baseColorFactor[0]),
					static_cast<float>(sourceMaterial.pbrMetallicRoughness.baseColorFactor[1]),
					static_cast<float>(sourceMaterial.pbrMetallicRoughness.baseColorFactor[2]),
					static_cast<float>(sourceMaterial.pbrMetallicRoughness.baseColorFactor[3]));
			}

			material->Params.MetallicFactor = static_cast<float>(sourceMaterial.pbrMetallicRoughness.metallicFactor);
			material->Params.RoughnessFactor = static_cast<float>(sourceMaterial.pbrMetallicRoughness.roughnessFactor);

			if (sourceMaterial.emissiveFactor.size() == 3)
			{
				material->EmissiveColor = glm::vec3(
					static_cast<float>(sourceMaterial.emissiveFactor[0]),
					static_cast<float>(sourceMaterial.emissiveFactor[1]),
					static_cast<float>(sourceMaterial.emissiveFactor[2]));
				material->Params.Emissive = material->EmissiveColor;
			}

			material->AlbedoTexture = LoadTextureFromTextureInfo(gltfModel, sourceMaterial.pbrMetallicRoughness.baseColorTexture.index, filepath, textureCache);
			material->MetallicTexture = LoadTextureFromTextureInfo(gltfModel, sourceMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, filepath, textureCache);
			material->RoughnessTexture = material->MetallicTexture;
			material->NormalTexture = LoadTextureFromTextureInfo(gltfModel, sourceMaterial.normalTexture.index, filepath, textureCache);
			material->AOTexture = LoadTextureFromTextureInfo(gltfModel, sourceMaterial.occlusionTexture.index, filepath, textureCache);
			material->EmissiveTexture = LoadTextureFromTextureInfo(gltfModel, sourceMaterial.emissiveTexture.index, filepath, textureCache);

			outMaterials.push_back(material);
		}

		std::vector<std::vector<uint32_t>> meshToPrimitiveIndices(gltfModel.meshes.size());
		for (size_t meshIndex = 0; meshIndex < gltfModel.meshes.size(); ++meshIndex)
		{
			const tinygltf::Mesh& sourceMesh = gltfModel.meshes[meshIndex];
			auto& primitiveIndices = meshToPrimitiveIndices[meshIndex];
			primitiveIndices.reserve(sourceMesh.primitives.size());

			for (size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex)
			{
				const tinygltf::Primitive& primitive = sourceMesh.primitives[primitiveIndex];
				if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
				{
					KBR_CORE_WARN("Skipping non-triangle glTF primitive in mesh '{}'", sourceMesh.name);
					continue;
				}

				if (!primitive.attributes.contains("POSITION"))
					throw std::runtime_error("glTF primitive missing POSITION attribute");

				const tinygltf::Accessor& positionsAccessor = gltfModel.accessors[primitive.attributes.at("POSITION")];
				if (positionsAccessor.bufferView < 0)
					throw std::runtime_error("POSITION accessor has invalid buffer view");

				std::vector<Vertex> vertices;
				vertices.resize(positionsAccessor.count);

				for (size_t i = 0; i < positionsAccessor.count; ++i)
					vertices[i].Position = ReadVec3(gltfModel, positionsAccessor, i);

				if (primitive.attributes.contains("NORMAL"))
				{
					const tinygltf::Accessor& normalsAccessor = gltfModel.accessors[primitive.attributes.at("NORMAL")];
					for (size_t i = 0; i < normalsAccessor.count && i < vertices.size(); ++i)
						vertices[i].Normal = glm::normalize(ReadVec3(gltfModel, normalsAccessor, i));
				}

				if (primitive.attributes.contains("TEXCOORD_0"))
				{
					const tinygltf::Accessor& texAccessor = gltfModel.accessors[primitive.attributes.at("TEXCOORD_0")];
					for (size_t i = 0; i < texAccessor.count && i < vertices.size(); ++i)
						vertices[i].TexCoord = ReadVec2(gltfModel, texAccessor, i);
				}

				bool hasTangents = false;
				if (primitive.attributes.contains("TANGENT"))
				{
					const tinygltf::Accessor& tangentAccessor = gltfModel.accessors[primitive.attributes.at("TANGENT")];
					for (size_t i = 0; i < tangentAccessor.count && i < vertices.size(); ++i)
						vertices[i].Tangent = ReadVec4(gltfModel, tangentAccessor, i);
					hasTangents = true;
				}

				std::vector<uint32_t> indices;
				if (primitive.indices >= 0)
				{
					const tinygltf::Accessor& indexAccessor = gltfModel.accessors[primitive.indices];
					indices = ReadIndices(gltfModel, indexAccessor);
				}
				else
				{
					indices.resize(vertices.size());
					for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i)
						indices[i] = i;
				}

				if (!hasTangents)
					GenerateTangentsForVertices(vertices, indices);

				std::string primitiveName = sourceMesh.name.empty()
					? "Mesh_" + std::to_string(meshIndex) + "_Primitive_" + std::to_string(primitiveIndex)
					: sourceMesh.name + "_Primitive_" + std::to_string(primitiveIndex);

				ModelPrimitive outPrimitive;
				outPrimitive.Name = primitiveName;
				outPrimitive.Mesh = CreateRef<Mesh>(primitiveName, vertices, indices);
				outPrimitive.SourceMeshIndex = static_cast<int32_t>(meshIndex);
				outPrimitive.SourcePrimitiveIndex = static_cast<int32_t>(primitiveIndex);
				if (primitive.material >= 0 && primitive.material < static_cast<int>(outMaterials.size()))
					outPrimitive.Material = outMaterials[primitive.material];

				primitiveIndices.push_back(static_cast<uint32_t>(outPrimitives.size()));
				outPrimitives.push_back(std::move(outPrimitive));
			}
		}

		outNodes.resize(gltfModel.nodes.size());
		for (size_t nodeIndex = 0; nodeIndex < gltfModel.nodes.size(); ++nodeIndex)
		{
			const tinygltf::Node& sourceNode = gltfModel.nodes[nodeIndex];
			ModelNode& outNode = outNodes[nodeIndex];

			outNode.Name = sourceNode.name.empty() ? "Node_" + std::to_string(nodeIndex) : sourceNode.name;
			outNode.SkinIndex = sourceNode.skin;

			if (sourceNode.translation.size() == 3)
			{
				outNode.Translation = glm::vec3(
					static_cast<float>(sourceNode.translation[0]),
					static_cast<float>(sourceNode.translation[1]),
					static_cast<float>(sourceNode.translation[2]));
			}
			if (sourceNode.rotation.size() == 4)
			{
				outNode.Rotation = glm::quat(
					static_cast<float>(sourceNode.rotation[3]),
					static_cast<float>(sourceNode.rotation[0]),
					static_cast<float>(sourceNode.rotation[1]),
					static_cast<float>(sourceNode.rotation[2]));
			}
			if (sourceNode.scale.size() == 3)
			{
				outNode.Scale = glm::vec3(
					static_cast<float>(sourceNode.scale[0]),
					static_cast<float>(sourceNode.scale[1]),
					static_cast<float>(sourceNode.scale[2]));
			}

			outNode.Children.reserve(sourceNode.children.size());
			for (const int child : sourceNode.children)
				outNode.Children.push_back(child);

			if (sourceNode.mesh >= 0 && sourceNode.mesh < static_cast<int>(meshToPrimitiveIndices.size()))
				outNode.PrimitiveIndices = meshToPrimitiveIndices[sourceNode.mesh];
		}

		for (size_t nodeIndex = 0; nodeIndex < gltfModel.nodes.size(); ++nodeIndex)
		{
			for (const int child : gltfModel.nodes[nodeIndex].children)
			{
				if (child >= 0 && child < static_cast<int>(outNodes.size()))
					outNodes[child].ParentIndex = static_cast<int32_t>(nodeIndex);
			}
		}

		outSkins.reserve(gltfModel.skins.size());

		// After populating model data, attempt to export materials and a prefab file when running in the Editor
		if (Project::GetActive() && Project::GetActive()->GetEditorAssetManager())
		{
			try
			{
				const std::filesystem::path projectAssetDir = Project::GetAssetFileSystemPath("");
				std::filesystem::path exportFolder = Project::GetProjectDirectory() / Project::GetActive()->GetInfo().AssetDirectory / "ImportedModels" / filepath.stem();
				std::filesystem::create_directories(exportFolder);

				// Export mesh primitives as .kbrmesh files
				for (size_t primIndex = 0; primIndex < outPrimitives.size(); ++primIndex)
				{
					auto& prim = outPrimitives[primIndex];
					if (!prim.Mesh)
						continue;

					std::string meshFileName = prim.Name + ".kbrmesh";
					std::filesystem::path meshPath = exportFolder / meshFileName;

					std::ofstream mout(meshPath, std::ios::binary);
					if (!mout.is_open())
					{
						KBR_CORE_ERROR("Failed to write mesh file: {}", meshPath.string());
						continue;
					}

					const auto& vertices = prim.Mesh->GetVertices();
					const auto& indices = prim.Mesh->GetIndices();

					uint32_t version = 1;
					uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
					uint32_t indexCount = static_cast<uint32_t>(indices.size());

					mout.write(reinterpret_cast<const char*>(&version), sizeof(version));
					mout.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
					mout.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

					for (const auto& v : vertices)
					{
						mout.write(reinterpret_cast<const char*>(&v.Position), sizeof(v.Position));
						mout.write(reinterpret_cast<const char*>(&v.Normal), sizeof(v.Normal));
						mout.write(reinterpret_cast<const char*>(&v.Tangent), sizeof(v.Tangent));
						mout.write(reinterpret_cast<const char*>(&v.TexCoord), sizeof(v.TexCoord));
					}

					mout.write(reinterpret_cast<const char*>(indices.data()), sizeof(uint32_t) * indices.size());
					mout.close();

					// Register mesh asset with editor asset manager
					Project::GetActive()->GetEditorAssetManager()->ImportAsset(meshPath);
				}

				// Export materials
				for (size_t i = 0; i < outMaterials.size(); ++i)
				{
					auto& mat = outMaterials[i];
					std::string matName = mat->Name.empty() ? ("Material_" + std::to_string(i)) : mat->Name;
					std::filesystem::path matPath = exportFolder / (matName + ".kbrmat");

					YAML::Emitter out;
					out << YAML::BeginMap;
					out << YAML::Key << "Material" << YAML::Value;
					out << YAML::BeginMap;
					out << YAML::Key << "Name" << YAML::Value << matName;
					out << YAML::Key << "Params" << YAML::Value;
					out << YAML::BeginMap;
					out << YAML::Key << "AlbedoFactor" << YAML::Value << YAML::Flow << YAML::BeginSeq << mat->Params.AlbedoFactor.r << mat->Params.AlbedoFactor.g << mat->Params.AlbedoFactor.b << mat->Params.AlbedoFactor.a << YAML::EndSeq;
					out << YAML::Key << "MetallicFactor" << YAML::Value << mat->Params.MetallicFactor;
					out << YAML::Key << "RoughnessFactor" << YAML::Value << mat->Params.RoughnessFactor;
					out << YAML::EndMap; // Params
					if (mat->EmissiveColor != glm::vec3(0.0f))
					{
						out << YAML::Key << "EmissiveColor" << YAML::Value << YAML::Flow << YAML::BeginSeq << mat->EmissiveColor.r << mat->EmissiveColor.g << mat->EmissiveColor.b << YAML::EndSeq;
					}
					out << YAML::EndMap; // Material

					std::ofstream fout(matPath);
					fout << out.c_str();
					fout.close();

					// Register imported material asset in editor asset manager
					Project::GetActive()->GetEditorAssetManager()->ImportAsset(matPath);
				}

				// Export a prefab YAML with proper UUID-based entity references
				std::filesystem::path prefabPath = exportFolder / (filepath.stem().string() + ".kbrprefab");
				Ref<Prefab> existingPrefab = nullptr;
				if (std::filesystem::exists(prefabPath))
					existingPrefab = PrefabSerializer::DeserializePrefab(prefabPath);

				// Generate UUIDs for each node (mapping nodeIndex -> UUID)
				std::vector<UUID> nodeUUIDs;
				nodeUUIDs.reserve(outNodes.size());
				for (size_t i = 0; i < outNodes.size(); ++i)
					nodeUUIDs.push_back(UUID());

				// Preserve UUIDs across re-imports when we can map nodes by stable index.
				if (existingPrefab && existingPrefab->Entities.size() == outNodes.size())
				{
					for (size_t i = 0; i < outNodes.size(); ++i)
					{
						if (existingPrefab->Entities[i].ID.IsValid())
							nodeUUIDs[i] = existingPrefab->Entities[i].ID;
					}
				}
				
				// Determine root entity (first node without parent or with parent index -1)
				UUID rootEntityID = UUID::Invalid();
				if (existingPrefab && existingPrefab->RootEntityID.IsValid())
				{
					for (const UUID uuid : nodeUUIDs)
					{
						if (uuid == existingPrefab->RootEntityID)
						{
							rootEntityID = uuid;
							break;
						}
					}
				}

				if (!rootEntityID.IsValid() && !outNodes.empty())
				{
					rootEntityID = nodeUUIDs[0];
					if (outNodes[0].ParentIndex >= 0 && static_cast<size_t>(outNodes[0].ParentIndex) < outNodes.size())
					{
						// Find first node without a valid parent
						for (size_t i = 0; i < outNodes.size(); ++i)
						{
							if (outNodes[i].ParentIndex < 0 || static_cast<size_t>(outNodes[i].ParentIndex) >= outNodes.size())
							{
								rootEntityID = nodeUUIDs[i];
								break;
							}
						}
					}
				}

				YAML::Emitter prefabOut;
				prefabOut << YAML::BeginMap;
				prefabOut << YAML::Key << "Prefab" << YAML::Value << YAML::BeginMap;
				prefabOut << YAML::Key << "Name" << YAML::Value << filepath.stem().string();
				prefabOut << YAML::Key << "RootEntityID" << YAML::Value << static_cast<uint64_t>(rootEntityID);
				prefabOut << YAML::EndMap; // Prefab

				prefabOut << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
				for (size_t nodeIndex = 0; nodeIndex < outNodes.size(); ++nodeIndex)
				{
					auto& node = outNodes[nodeIndex];
					prefabOut << YAML::BeginMap;
					prefabOut << YAML::Key << "ID" << YAML::Value << static_cast<uint64_t>(nodeUUIDs[nodeIndex]);
					prefabOut << YAML::Key << "Tag" << YAML::Value << node.Name;
					
					// Convert translation
					prefabOut << YAML::Key << "Translation" << YAML::Value << YAML::Flow << YAML::BeginSeq << node.Translation.x << node.Translation.y << node.Translation.z << YAML::EndSeq;
					
					// Convert quaternion rotation to euler angles
					glm::vec3 eulerAngles = glm::eulerAngles(node.Rotation);
					prefabOut << YAML::Key << "EulerRotation" << YAML::Value << YAML::Flow << YAML::BeginSeq << eulerAngles.x << eulerAngles.y << eulerAngles.z << YAML::EndSeq;
					
					// Scale
					prefabOut << YAML::Key << "Scale" << YAML::Value << YAML::Flow << YAML::BeginSeq << node.Scale.x << node.Scale.y << node.Scale.z << YAML::EndSeq;
					
					// Parent reference
					UUID parentUUID = UUID::Invalid();
					if (node.ParentIndex >= 0 && static_cast<size_t>(node.ParentIndex) < nodeUUIDs.size())
						parentUUID = nodeUUIDs[node.ParentIndex];
					prefabOut << YAML::Key << "Parent" << YAML::Value << static_cast<uint64_t>(parentUUID);
					
					// Children references
					prefabOut << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
					for (const int child : node.Children)
					{
						if (child >= 0 && static_cast<size_t>(child) < nodeUUIDs.size())
							prefabOut << static_cast<uint64_t>(nodeUUIDs[child]);
					}
					prefabOut << YAML::EndSeq;
					
					// StaticMeshComponent data (if has primitives)
					prefabOut << YAML::Key << "HasStaticMesh" << YAML::Value << (!node.PrimitiveIndices.empty());
					if (!node.PrimitiveIndices.empty())
					{
						// Store mesh and material paths for later resolution
						// For now, we use placeholder paths - these should be updated to actual asset paths
						// TODO: resolve to actual mesh/material asset paths
						prefabOut << YAML::Key << "MeshAssetPath" << YAML::Value << "";
						prefabOut << YAML::Key << "MaterialAssetPath" << YAML::Value << "";
					}
					
					prefabOut << YAML::EndMap; // Entity
				}
				prefabOut << YAML::EndSeq; // Entities
				prefabOut << YAML::EndMap; // Root

				std::ofstream pfout(prefabPath);
				if (pfout.is_open())
				{
					pfout << prefabOut.c_str();
					pfout.close();
					Project::GetActive()->GetEditorAssetManager()->ImportAsset(prefabPath);
				}
			}
			catch (const std::exception& e)
			{
				KBR_CORE_ERROR("GLTF import export error: {}", e.what());
			}
		}

		for (size_t skinIndex = 0; skinIndex < gltfModel.skins.size(); ++skinIndex)
		{
			const tinygltf::Skin& sourceSkin = gltfModel.skins[skinIndex];
			ModelSkin outSkin{};
			outSkin.Name = sourceSkin.name.empty() ? "Skin_" + std::to_string(skinIndex) : sourceSkin.name;
			outSkin.SkeletonRootNode = sourceSkin.skeleton;
			outSkin.JointNodes.reserve(sourceSkin.joints.size());
			for (const int jointNode : sourceSkin.joints)
				outSkin.JointNodes.push_back(jointNode);

			if (sourceSkin.inverseBindMatrices >= 0)
			{
				const tinygltf::Accessor& accessor = gltfModel.accessors[sourceSkin.inverseBindMatrices];
				const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[view.buffer];
				const size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 16;

				outSkin.InverseBindMatrices.reserve(accessor.count);
				for (size_t i = 0; i < accessor.count; ++i)
				{
					const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset + i * stride;
					const float* m = reinterpret_cast<const float*>(data);
					glm::mat4 matrix(1.0f);
					for (int col = 0; col < 4; ++col)
					{
						for (int row = 0; row < 4; ++row)
						{
							matrix[col][row] = m[col * 4 + row];
						}
					}
					outSkin.InverseBindMatrices.push_back(matrix);
				}
			}

			outSkins.push_back(std::move(outSkin));
		}

		outAnimations.reserve(gltfModel.animations.size());
		for (size_t animationIndex = 0; animationIndex < gltfModel.animations.size(); ++animationIndex)
		{
			const tinygltf::Animation& sourceAnimation = gltfModel.animations[animationIndex];
			ModelAnimationClip outClip{};
			outClip.Name = sourceAnimation.name.empty() ? "Animation_" + std::to_string(animationIndex) : sourceAnimation.name;
			outClip.Samplers.resize(sourceAnimation.samplers.size());

			for (size_t samplerIndex = 0; samplerIndex < sourceAnimation.samplers.size(); ++samplerIndex)
			{
				const tinygltf::AnimationSampler& sourceSampler = sourceAnimation.samplers[samplerIndex];
				ModelAnimationSampler& outSampler = outClip.Samplers[samplerIndex];
				outSampler.Interpolation = sourceSampler.interpolation.empty() ? "LINEAR" : sourceSampler.interpolation;

				const tinygltf::Accessor& inputAccessor = gltfModel.accessors[sourceSampler.input];
				const tinygltf::BufferView& inputView = gltfModel.bufferViews[inputAccessor.bufferView];
				const tinygltf::Buffer& inputBuffer = gltfModel.buffers[inputView.buffer];
				const size_t inputStride = inputView.byteStride ? inputView.byteStride : sizeof(float);

				outSampler.Inputs.reserve(inputAccessor.count);
				for (size_t i = 0; i < inputAccessor.count; ++i)
				{
					const uint8_t* data = inputBuffer.data.data() + inputView.byteOffset + inputAccessor.byteOffset + i * inputStride;
					const float* value = reinterpret_cast<const float*>(data);
					outSampler.Inputs.push_back(*value);
				}

				const tinygltf::Accessor& outputAccessor = gltfModel.accessors[sourceSampler.output];
				const tinygltf::BufferView& outputView = gltfModel.bufferViews[outputAccessor.bufferView];
				const tinygltf::Buffer& outputBuffer = gltfModel.buffers[outputView.buffer];
				size_t outputElementSize = sizeof(float);
				if (outputAccessor.type == TINYGLTF_TYPE_VEC3)
					outputElementSize = sizeof(float) * 3;
				else if (outputAccessor.type == TINYGLTF_TYPE_VEC4)
					outputElementSize = sizeof(float) * 4;
				else if (outputAccessor.type == TINYGLTF_TYPE_SCALAR)
					outputElementSize = sizeof(float);
				const size_t outputStride = outputView.byteStride ? outputView.byteStride : outputElementSize;

				outSampler.Outputs.reserve(outputAccessor.count);
				for (size_t i = 0; i < outputAccessor.count; ++i)
				{
					const uint8_t* data = outputBuffer.data.data() + outputView.byteOffset + outputAccessor.byteOffset + i * outputStride;
					const float* values = reinterpret_cast<const float*>(data);
					if (outputAccessor.type == TINYGLTF_TYPE_VEC3)
						outSampler.Outputs.emplace_back(values[0], values[1], values[2], 0.0f);
					else if (outputAccessor.type == TINYGLTF_TYPE_VEC4)
						outSampler.Outputs.emplace_back(values[0], values[1], values[2], values[3]);
					else
						outSampler.Outputs.emplace_back(values[0], 0.0f, 0.0f, 0.0f);
				}
			}

			outClip.Channels.reserve(sourceAnimation.channels.size());
			for (const tinygltf::AnimationChannel& sourceChannel : sourceAnimation.channels)
			{
				ModelAnimationChannel channel{};
				channel.SamplerIndex = static_cast<uint32_t>(sourceChannel.sampler);
				channel.NodeIndex = sourceChannel.target_node;
				channel.Path = ToAnimationPath(sourceChannel.target_path);
				outClip.Channels.push_back(channel);
			}

			outAnimations.push_back(std::move(outClip));
		}

		return modelAsset;
	}
}
