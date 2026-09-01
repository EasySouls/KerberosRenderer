
#include "Core/Timer.hpp"
#include "AssimpModelImporter.hpp"
#include "TextureImporter.hpp"
#include "Renderer/Vertex.hpp"

#include <Assimp/scene.h>
#include <Assimp/postprocess.h>
#include <Assimp/material.h>
#include <Assimp/Importer.hpp>
#include <fstream>


import Kerberos;

namespace Kerberos
{
	Ref<Mesh> AssimpModelImporter::ImportModel(AssetHandle handle, const AssetMetadata& metadata)
	{
		return ImportModel(metadata.Filepath);
	}

	Ref<Mesh> AssimpModelImporter::ImportModel(const std::filesystem::path& filepath)
	{
		const auto res = LoadModel(filepath);

		if (!res)
		{
			switch (const auto error = res.error())
			{
				case ModelLoadingError::CannotOpenFile:
					Log::CoreError("Cannot open model file: {}", filepath.string());
					break;
				case ModelLoadingError::ImportFailed:
					Log::CoreError("Model import failed for file: {}", filepath.string());
					break;
				default:
					Log::CoreError("Unknown error occurred while loading model: {}", filepath.string());
					break;
			}
			return nullptr;
		}

		const auto info = res.value();
		if (info.submeshes.empty())
		{
			Log::CoreError("No meshes found in the model at {}", filepath.string());
			return nullptr;
		}
		return info.submeshes[0].Mesh;
	}

	std::expected<AssimpModelImporter::ModelLoadingInfo, AssimpModelImporter::ModelLoadingError> AssimpModelImporter::LoadModel(const std::filesystem::path& path)
	{
		Timer timer("Model Loading", [&](const TimerData& data)
		{
			Log::CoreInfo("Loading model from {} took {:.2f} ms", path.string(), data.DurationMs);
		});

		ModelLoadingInfo loadingInfo{};

		{
			std::ifstream file(path);
			if (!file.is_open())
			{
				Log::CoreError("Failed to open model file: {}", path.string());
				return std::unexpected(ModelLoadingError::CannotOpenFile);
			}
			const std::string data = std::string((std::istreambuf_iterator<char>(file)),
												 std::istreambuf_iterator<char>());

		}

		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path.string(),
												 aiProcess_Triangulate |
												 aiProcess_FlipUVs |
												 aiProcess_GenNormals |       // Good to have if models are missing them
												 aiProcess_CalcTangentSpace); // For normal mapping later

		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		{
			Log::CoreError("Assimp error: {}", importer.GetErrorString());
			return std::unexpected(ModelLoadingError::ImportFailed);
		}

		loadingInfo.directory = path.parent_path();

		ProcessMaterials(scene, loadingInfo);

		ProcessMeshes(scene, loadingInfo);
	}

	void AssimpModelImporter::ProcessMaterials(const aiScene* scene, ModelLoadingInfo& info)
	{
		Log::CoreTrace("Loading {} materials...", scene->mNumMaterials);
		info.materials.reserve(scene->mNumMaterials);

		for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
		{
			const aiMaterial* aiMat = scene->mMaterials[i];
			auto material = CreateRef<DeprecatedMaterial>();

			// Get material name
			aiString name;
			aiMat->Get(AI_MATKEY_NAME, name);
			material->Name = name.C_Str();

			// Get material colors
			aiColor3D color(0.f, 0.f, 0.f);
			if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
				material->Diffuse = glm::vec3(color.r, color.g, color.b);
			if (aiMat->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS)
				material->Ambient = glm::vec3(color.r, color.g, color.b);
			if (aiMat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
				material->Specular = glm::vec3(color.r, color.g, color.b);

			float shininess;
			if (aiMat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
				material->Shininess = shininess;

			// Load textures (assuming your material will hold them)
			// You should probably have Ref<Texture2D> DiffuseTexture, SpecularTexture, etc. in your Material class.
			// For now, let's just load the first diffuse texture we find.
			if (aiMat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
			{
				aiString str;
				aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &str);
				const std::filesystem::path texturePath = info.directory / str.C_Str();

				if (!info.loadedTextures.contains(texturePath))
				{
					/// Texture not loaded yet, load it
					info.loadedTextures[texturePath] = TextureImporter::ImportTexture(texturePath);
				}
				material->DiffuseTexture = info.loadedTextures[texturePath]; // Assign to material
			}

			info.materials.push_back(material);
		}

		/// Create a default material if none were loaded
		if (info.materials.empty())
		{
			const auto material = CreateRef<DeprecatedMaterial>();
			material->Name = "Default";
			info.materials.push_back(material);
		}
	}

	void AssimpModelImporter::ProcessMeshes(const aiScene* scene, ModelLoadingInfo& info)
	{
		// This map will hold all the geometry, grouped by material index.
		std::map<uint32_t, std::vector<aiMesh*>> meshesByMaterial;

		// Group all meshes from the scene by their material index.
		std::function<void(const aiNode*)> collectMeshes =
			[&](const aiNode* node)
		{
			for (unsigned int i = 0; i < node->mNumMeshes; ++i)
			{
				aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
				meshesByMaterial[mesh->mMaterialIndex].push_back(mesh);
			}
			for (unsigned int i = 0; i < node->mNumChildren; ++i)
			{
				collectMeshes(node->mChildren[i]);
			}
		};
		collectMeshes(scene->mRootNode);

		Log::CoreTrace("Model has been sorted into {} material groups.", meshesByMaterial.size());

		// Now, for each material group, merge the meshes into one.
		for (auto const& [materialIndex, meshGroup] : meshesByMaterial)
		{
			std::vector<Vertex> combinedVertices;
			std::vector<uint32_t> combinedIndices;
			uint32_t vertexOffset = 0;

			for (const aiMesh* mesh : meshGroup)
			{
				// Copy vertices
				for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
				{
					Vertex vertex{};
					vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
					if (mesh->HasNormals())
					{
						vertex.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
					}
					if (mesh->mTextureCoords[0])
					{
						vertex.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
					}
					combinedVertices.push_back(vertex);
				}

				// Copy indices, making sure to add the offset!
				for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
				{
					const aiFace& face = mesh->mFaces[i];
					for (unsigned int j = 0; j < face.mNumIndices; ++j)
					{
						combinedIndices.push_back(face.mIndices[j] + vertexOffset);
					}
				}

				// Update the vertex offset for the next mesh in this group
				vertexOffset += mesh->mNumVertices;
			}

			// Create a single mesh for this material group
			const auto mergedMesh = CreateRef<Mesh>(combinedVertices, combinedIndices);

			// Find the corresponding material
			const Ref<DeprecatedMaterial> material = (info.materials.size() > materialIndex) ? info.materials[materialIndex] : info.materials.front();

			// Store the submesh
			info.submeshes.push_back({ .Mesh = mergedMesh, .Material = material });
		}
	}
}