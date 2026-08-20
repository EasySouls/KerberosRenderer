#pragma once

#include "Renderer/Vertex.hpp"
#include "Scene/AABB.hpp"
#include "Assets/Asset.hpp"
#include "Buffer.hpp"

#include <vector>
#include <string>

namespace Kerberos 
{

class SkeletalMesh : public Asset
{
public:
    SkeletalMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    SkeletalMesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    ~SkeletalMesh() override = default;

    SkeletalMesh(const SkeletalMesh&) = delete;
    SkeletalMesh& operator=(const SkeletalMesh&) = delete;

    SkeletalMesh(SkeletalMesh&&) = default;
    SkeletalMesh& operator=(SkeletalMesh&&) = default;

    void Draw(vk::CommandBuffer commandBuffer) const;

    void SetDebugName(const std::string& name) const;

    AssetType GetType() override
    {
        return AssetType::Mesh;
    }

    const std::vector<Vertex>& GetVertices() const
    {
        return m_Vertices;
    }
    const std::vector<uint32_t>& GetIndices() const
    {
        return m_Indices;
    }

    const VertexBuffer& GetVertexBuffer() const
    {
        return m_VertexBuffer;
    }
    const IndexBuffer& GetIndexBuffer() const
    {
        return m_IndexBuffer;
    }

    const AABB& GetBoundingBox() const
    {
        return m_BoundingBox;
    }

private:
    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;

    std::string m_Name;

    VertexBuffer m_VertexBuffer;
    IndexBuffer m_IndexBuffer;

    AABB m_BoundingBox;
};

}