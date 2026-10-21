#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Kerberos {

struct MeshFileHeader
{
    uint32_t Magic; // "KBRM"
    uint16_t Version;
    uint16_t VertexStride;
    uint32_t VertexCount;
    uint32_t IndexCount;
    uint32_t Flags; // Has skinning, has tangents, etc.
};

struct SkeletalVertexPacked
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec4 Tangent;
    glm::vec2 UV0;
    glm::uvec4 JointIndices;
    glm::vec4 JointWeights;
};

}