#include "ModelLoader.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <iostream>
#include <tiny_gltf.h>

namespace kbr
{
	Mesh ModelLoader::LoadModel(const std::filesystem::path& path)
	{
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;
		bool ret = false;
		if (path.extension() == ".glb")
		{
			ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
		}
		else
		{
			ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
		}
		if (!warn.empty())
		{
			std::cout << "GLTF Warning: " << warn << '\n';
		}
		if (!err.empty())
		{
			std::cerr << "GLTF Error: " << err << '\n';
		}
		if (!ret)
		{
			throw std::runtime_error("Failed to load glTF model: " + path.string());
		}

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		
        // Process all meshes in the model
        std::unordered_map<Vertex, uint32_t> uniqueVertices{};

        for (const auto& mesh : model.meshes) {
            for (const auto& primitive : mesh.primitives) {
                // Get indices
                const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

                // Get vertex positions
                const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
                const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

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

                // Process vertices
                for (size_t i = 0; i < posAccessor.count; i++) {
                    Vertex vertex{};

                    // Get position
                    const float* pos = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset + i * 12]);
                    vertex.pos = { pos[0], pos[1], pos[2] };

                    // Get texture coordinates if available
                    if (hasTexCoords) {
                        const float* texCoord = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * 8]);
                        vertex.texCoord = { texCoord[0], 1.0f - texCoord[1] };
                    }
                    else {
                        vertex.texCoord = { 0.0f, 0.0f };
                    }

                    // Set default color
                    vertex.color = { 1.0f, 1.0f, 1.0f };

                    // Add vertex if unique
                    if (!uniqueVertices.contains(vertex)) {
                        uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                        vertices.push_back(vertex);
                    }
                }

                // Process indices
                const unsigned char* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];

                // Handle different index component types
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* indices16 = reinterpret_cast<const uint16_t*>(indexData);
                    for (size_t i = 0; i < indexAccessor.count; i++) {
                        Vertex vertex = vertices[indices16[i]];
                        indices.push_back(uniqueVertices[vertex]);
                    }
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    const uint32_t* indices32 = reinterpret_cast<const uint32_t*>(indexData);
                    for (size_t i = 0; i < indexAccessor.count; i++) {
                        Vertex vertex = vertices[indices32[i]];
                        indices.push_back(uniqueVertices[vertex]);
                    }
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    const uint8_t* indices8 = reinterpret_cast<const uint8_t*>(indexData);
                    for (size_t i = 0; i < indexAccessor.count; i++) {
                        Vertex vertex = vertices[indices8[i]];
                        indices.push_back(uniqueVertices[vertex]);
                    }
                }
            }
        }

		const std::string name = path.stem().string();
        return { name, vertices, indices };
	}
}
