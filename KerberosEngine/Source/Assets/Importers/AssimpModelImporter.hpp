#pragma once

#include "Core/Core.hpp"
#include "Assets/Asset.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"

#include <filesystem>
#include <expected>
#include <map>

struct aiScene;

namespace Kerberos
{
    class Texture2D;

    struct DeprecatedMaterial
    {
        std::string Name;
        glm::vec3 Diffuse{ 1.0f };
		glm::vec3 Specular{ 1.0f };
		glm::vec3 Ambient{ 1.0f };
		float Shininess = 32.0f;
		Ref<Texture2D> DiffuseTexture;
	};

    struct Submesh
    {
        Ref<Mesh> Mesh;
        Ref<DeprecatedMaterial> Material;
    };

    class AssimpModelImporter
    {
    public:
        static Ref<Mesh> ImportModel(AssetHandle handle, const AssetMetadata& metadata);
        static Ref<Mesh> ImportModel(const std::filesystem::path& filepath);

    private:
        struct ModelLoadingInfo
        {
            // This is our new primary data structure
            std::vector<Submesh> submeshes;

            std::filesystem::path directory;

            // We can store loaded textures in a map to prevent reloading
            // The key is the path, the value is the loaded texture
            std::map<std::filesystem::path, Ref<Texture2D>> loadedTextures;

            // This will hold the materials loaded from the scene
            std::vector<Ref<DeprecatedMaterial>> materials;

            std::string name;
        };

        enum class ModelLoadingError
        {
            CannotOpenFile,
            ImportFailed
        };

    private:
        static std::expected<ModelLoadingInfo, ModelLoadingError> LoadModel(const std::filesystem::path& path);

        static void ProcessMaterials(const aiScene* scene, ModelLoadingInfo& info);
        static void ProcessMeshes(const aiScene* scene, ModelLoadingInfo& info);
    };
}