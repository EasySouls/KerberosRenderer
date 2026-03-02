#pragma once

#include "Core/Core.hpp"
#include "Assets/Asset.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Texture.hpp"

#include <filesystem>

struct aiScene;

namespace Kerberos
{
    struct Submesh
    {
        Ref<Mesh> Mesh;
        Ref<Material> Material;
    };

    class MeshImporter
    {
    public:
        Ref<Mesh> ImportMesh(AssetHandle handle, const AssetMetadata& metadata);
        Ref<Mesh> ImportMesh(const std::filesystem::path& filepath);

    private:
        void LoadModel(const std::filesystem::path& path);

        void ProcessMaterials(const aiScene* scene);
        void ProcessMeshes(const aiScene* scene);

    private:
        // This is our new primary data structure
        std::vector<Submesh> m_Submeshes;

        std::filesystem::path m_Directory;

        // We can store loaded textures in a map to prevent reloading
        // The key is the path, the value is the loaded texture
        std::map<std::filesystem::path, Ref<Texture2D>> m_LoadedTextures;

        // This will hold the materials loaded from the scene
        std::vector<Ref<Material>> m_Materials;

        std::string m_Name;
    };
}