#pragma once

#include "Scene/AABB.hpp"
#include "Assets/Asset.hpp"

#include <vector>
#include <string>

namespace Kerberos 
{

struct SkinVertex
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec4 Tangent;
    glm::vec2 TexCoord;
    glm::uvec4 JointIndices;
    glm::vec4 JointWeights;
};

class SkeletalMesh : public Asset
{
public:
    SkeletalMesh(const std::vector<SkinVertex>& vertices, const std::vector<uint32_t>& indices);
    SkeletalMesh(const std::string& name, const std::vector<SkinVertex>& vertices, const std::vector<uint32_t>& indices);
    ~SkeletalMesh() override = default;

    SkeletalMesh(const SkeletalMesh&) = delete;
    SkeletalMesh& operator=(const SkeletalMesh&) = delete;

    SkeletalMesh(SkeletalMesh&&) = default;
    SkeletalMesh& operator=(SkeletalMesh&&) = default;

    static constexpr AssetType Type = AssetType::SkinnedMesh;

    AssetType GetType() override
    {
        return Type;
    }

    const std::vector<SkinVertex>& GetVertices() const
    {
        return m_Vertices;
    }
    const std::vector<uint32_t>& GetIndices() const
    {
        return m_Indices;
    }

    const AABB& GetBoundingBox() const
    {
        return m_BoundingBox;
    }

private:
    std::vector<SkinVertex> m_Vertices;
    std::vector<uint32_t> m_Indices;

    std::string m_Name;

    AABB m_BoundingBox;
};

}