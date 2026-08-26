#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

struct SkeletonJointFile
{
    std::string Name;
    int32_t ParentJoint = -1;
    glm::mat4 InverseBind;
};

struct SkeletonFile
{
    uint32_t Version = 1;
    std::vector<SkeletonJointFile> Joints;
};

}