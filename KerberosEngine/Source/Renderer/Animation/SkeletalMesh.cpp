#include "kbrpch.hpp"
#include "SkeletalMesh.hpp"

#include "Buffer.hpp"
#include "Utils.hpp"

import Kerberos;

namespace Kerberos 
{

SkeletalMesh::SkeletalMesh(const std::vector<SkinVertex>& vertices, const std::vector<uint32_t>& indices)
    : m_Vertices(vertices), m_Indices(indices)
{
    // Ensure weight are summed to 1.0 for each vertex
    for (const auto& vertex : m_Vertices)
    {
        float weightSum = vertex.JointWeights.x + vertex.JointWeights.y + vertex.JointWeights.z + vertex.JointWeights.w;
        KBRAssert(std::abs(weightSum - 1.0f) < 0.001f, "Joint weights do not sum to 1.0 for a vertex");
    }

    AABB boundingBox;

    for (const auto& vertex : vertices) {
        boundingBox.Min = glm::min(boundingBox.Min, vertex.Position);
        boundingBox.Max = glm::max(boundingBox.Max, vertex.Position);
    }
    m_BoundingBox = boundingBox;
}

SkeletalMesh::SkeletalMesh(const std::string& name,
                           const std::vector<SkinVertex>& vertices,
                           const std::vector<uint32_t>& indices)
    : SkeletalMesh(vertices, indices)
{
    m_Name = name;
}

}
