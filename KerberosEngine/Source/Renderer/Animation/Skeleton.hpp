#pragma once

#include "Assets/Asset.hpp"
#include "Core/UUID.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <string>

namespace Kerberos {

struct SkeletonJoint
{
    std::string Name;
    int32_t Parent = -1; // Joint-space parent
    int32_t SourceNode = -1; // glTF node index, for prefab mapping
    glm::vec3 RestTranslation{ 0.0f };
    glm::quat RestRotation{ 1, 0, 0, 0 };
    glm::vec3 RestScale{ 1.0f };
    glm::mat4 InverseBind{ 1.0f };
};

class Skeleton : public Asset
{
public:
    explicit Skeleton(const std::vector<SkeletonJoint>& joints);

    ~Skeleton() override = default;

    Skeleton(const Skeleton&) = delete;
    Skeleton& operator=(const Skeleton&) = delete;

    Skeleton(Skeleton&&) = default;
    Skeleton& operator=(Skeleton&&) = default;

    static constexpr AssetType Type = AssetType::Skeleton;
    AssetType GetType() override
    {
        return Type;
    }

    const std::vector<SkeletonJoint>& GetJoints() const
    {
        return m_Joints;
    }

    const SkeletonJoint& GetJointBySourceNode(int32_t sourceNode) const;

private:
    std::vector<SkeletonJoint> m_Joints;
};

}