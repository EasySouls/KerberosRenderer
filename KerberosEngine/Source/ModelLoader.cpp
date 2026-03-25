#include "kbrpch.hpp"
#include "ModelLoader.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <iostream>

namespace Kerberos
{
	Mesh ModelLoader::LoadModel(const std::filesystem::path& path, GLTFLoadingFlags flags)
	{
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;
		bool ret = false;

		const std::filesystem::path currentPath = std::filesystem::current_path();
		const std::filesystem::path fullPath = currentPath / path;
		const std::filesystem::path relativePath = std::filesystem::relative(fullPath, currentPath);

		if (path.extension() == ".glb")
		{
			ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
		}
		else if (path.extension() == ".gltf")
		{
			ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
		}
		else 
		{
			KBR_CORE_ERROR("Unsupported file format: {0}", path.extension().string());
			throw std::runtime_error("Unsupported file format: " + path.extension().string());
		}

		if (!warn.empty())
		{
			KBR_CORE_WARN("GLTF Warning: {0}", warn);
		}
		if (!err.empty())
		{
			KBR_CORE_ERROR("GLTF Error: {0}", err);
		}
		if (!ret)
		{
			KBR_CORE_ASSERT(false, "Failed to load glTF model: {0}", path.string());
			throw std::runtime_error("Failed to load glTF model: " + path.string());
		}

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		
        // Process all meshes in the model
        std::unordered_map<Vertex, uint32_t> uniqueVertices{};

        for (const auto& mesh : model.meshes) 
        {
            for (const auto& primitive : mesh.primitives) 
            {
				if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                    throw std::runtime_error("Only triangle primitives are supported.");
                }

				if (!primitive.attributes.contains("POSITION") || primitive.indices < 0) {
                    throw std::runtime_error("Primitive is missing POSITION attribute or indices.");
                }

                // Get indices
                const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

                // Get vertex positions
                const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
                const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

				// Get vertex normals
				const tinygltf::Accessor* normalAccessor = nullptr;
				const tinygltf::BufferView* normalBufferView = nullptr;
				const tinygltf::Buffer* normalBuffer = nullptr;
				bool hasNormals = primitive.attributes.contains("NORMAL");
				if (hasNormals) {
					normalAccessor = &model.accessors[primitive.attributes.at("NORMAL")];
					normalBufferView = &model.bufferViews[normalAccessor->bufferView];
					normalBuffer = &model.buffers[normalBufferView->buffer];
				}

                // Get texture coordinates if available
                bool hasTexCoords = primitive.attributes.contains("TEXCOORD_0");
                const tinygltf::Accessor* texCoordAccessor = nullptr;
                const tinygltf::BufferView* texCoordBufferView = nullptr;
                const tinygltf::Buffer* texCoordBuffer = nullptr;

                if (hasTexCoords) {
                    texCoordAccessor = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
                    texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
                }

				bool hasTangents = primitive.attributes.contains("TANGENT");
				const tinygltf::Accessor* tangentAccessor = nullptr;
				const tinygltf::BufferView* tangentBufferView = nullptr;
				const tinygltf::Buffer* tangentBuffer = nullptr;
				if (hasTangents) {
					tangentAccessor = &model.accessors[primitive.attributes.at("TANGENT")];
					tangentBufferView = &model.bufferViews[tangentAccessor->bufferView];
					tangentBuffer = &model.buffers[tangentBufferView->buffer];
				}

				const size_t posStride = posBufferView.byteStride ? posBufferView.byteStride : sizeof(float) * 3;
				const size_t normalStride = hasNormals && normalBufferView->byteStride ? normalBufferView->byteStride : sizeof(float) * 3;
				const size_t uvStride = hasTexCoords && texCoordBufferView->byteStride ? texCoordBufferView->byteStride : sizeof(float) * 2;
				const size_t tangentStride = hasTangents && tangentBufferView->byteStride ? tangentBufferView->byteStride : sizeof(float) * 4;

                std::vector<uint32_t> remap(posAccessor.count);

                // Process vertices
                for (size_t i = 0; i < posAccessor.count; i++) 
				{
					Vertex vertex{};

					const uint8_t* posBytes = posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset + i * posStride;
					const float* pos = reinterpret_cast<const float*>(posBytes);
					vertex.Position = { pos[0], pos[1], pos[2] };

					if (hasNormals)
					{
						const uint8_t* normalBytes = normalBuffer->data.data() + normalBufferView->byteOffset + normalAccessor->byteOffset + i * normalStride;
						const float* normal = reinterpret_cast<const float*>(normalBytes);
						vertex.Normal = { normal[0], normal[1], normal[2] };
					}
					else
					{
						vertex.Normal = { 0.0f, 0.0f, 0.0f };
					}

					if (hasTexCoords)
					{
						const uint8_t* uvBytes = texCoordBuffer->data.data() + texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * uvStride;
						const float* texCoord = reinterpret_cast<const float*>(uvBytes);
						//vertex.texCoord = { texCoord[0], 1.0f - texCoord[1] };
						vertex.TexCoord = { texCoord[0], texCoord[1] };
					}
					else
					{
						vertex.TexCoord = { 0.0f, 0.0f };
					}

					if (hasTangents)
					{
						const uint8_t* tangentBytes = tangentBuffer->data.data() + tangentBufferView->byteOffset + tangentAccessor->byteOffset + i * tangentStride;
						const float* tangent = reinterpret_cast<const float*>(tangentBytes);
						vertex.Tangent = { tangent[0], tangent[1], tangent[2], tangent[3] };
					}
					else
					{
						vertex.Tangent = { 0.0f, 0.0f, 0.0f, 0.0f };
					}

					auto it = uniqueVertices.find(vertex);
					if (it == uniqueVertices.end())
					{
						const uint32_t newIndex = static_cast<uint32_t>(vertices.size());
						uniqueVertices.emplace(vertex, newIndex);
						vertices.push_back(vertex);
						remap[i] = newIndex;
					}
					else
					{
						remap[i] = it->second;
					}
                }

                // Process indices
                const uint8_t* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];

				auto emitIndex = [&](const uint32_t gltfVertexIndex)
				{
					if (gltfVertexIndex >= remap.size())
						throw std::runtime_error("glTF index out of range for POSITION accessor");

					indices.push_back(remap[gltfVertexIndex]);
				};

                // Handle different index component types
				if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					const uint16_t* idx = reinterpret_cast<const uint16_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; i++) 
						emitIndex(idx[i]);
				}
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
				{
					const uint32_t* idx = reinterpret_cast<const uint32_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; i++) 
						emitIndex(idx[i]);
				}
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
				{
					const uint8_t* idx = reinterpret_cast<const uint8_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; i++) 
						emitIndex(idx[i]);
				}
				else
				{
					throw std::runtime_error("Unsupported index component type");
				}
            }
        }

		if ((flags & GLTFLoadingFlags::FlipY) == GLTFLoadingFlags::FlipY)
		{
			for (auto& vertex : vertices)
			{
				vertex.Position.y *= -1.0f;
				vertex.Normal.y *= -1.0f;
			}

			for (size_t i = 0; i < indices.size(); i += 3)
			{
				std::swap(indices[i + 1], indices[i + 2]);
			}
		}

		constexpr float epsilon = 1e-4f;
		for (auto& vertex : vertices)
		{
			for (int i = 0; i < 3; i++)
			{
				if (std::abs(vertex.Normal[i]) < epsilon)
					vertex.Normal[i] = 0.0f;
			}

			if (glm::length(vertex.Normal) > 0.0f) 
			{
				vertex.Normal = glm::normalize(vertex.Normal);
			}
		}

		const std::string name = path.stem().string();
        return { name, vertices, indices };
	}
}
