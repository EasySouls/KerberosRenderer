#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

struct SkeletonJointFile
{
    std::string Name;
    int32_t ParentJoint = -1; // Joint-space parent, topological order
    int32_t SourceNode = -1;  // glTF node index
    glm::vec3 RestTranslation = glm::vec3(0.0f);
    glm::quat RestRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 RestScale = glm::vec3(1.0f);
    glm::mat4 InverseBind = glm::mat4(1.0f);
};

struct SkeletonFile
{
    uint32_t Version = 1;
    std::string LocalKey; // "skeleton:<i>"
    uint32_t SourceIndex = 0;
    std::string Name;
    std::vector<SkeletonJointFile> Joints;
};

}
