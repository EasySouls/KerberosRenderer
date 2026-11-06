#include "Skeleton.hpp"

#include "Profiling/TracyInstrumentation.hpp"

#include <ranges>

import Kerberos;

namespace Kerberos {

Skeleton::Skeleton(const std::vector<SkeletonJoint>& joints)
    : m_Joints(joints)
{
    // Ensure that joint count is fewer than 65535, as we use 16-bit indices for joint references in the GPU skinning
    // system.
    KBRAssert(m_Joints.size() < 65535, "Skeleton has too many joints: {}", m_Joints.size());

    // Ensure that the joints are topologically sorted
    for (size_t i = 0; i < m_Joints.size(); ++i)
    {
        KBRAssert(m_Joints[i].Parent == -1 || m_Joints[i].Parent < static_cast<int32_t>(i), "Joint {} is not topologically sorted", i);

        if (!m_Joints[i].Name.empty())
            m_JointByName.try_emplace(m_Joints[i].Name, static_cast<int32_t>(i));
    }
}

int32_t Skeleton::GetJointIndex(const std::string_view name) const
{
    KBR_TRACY_FUNCTION();

    const auto it = m_JointByName.find(std::string(name));
    return it != m_JointByName.end() ? it->second : -1;
}

const SkeletonJoint& Skeleton::GetJointBySourceNode(int32_t sourceNode) const
{
    KBR_TRACY_FUNCTION();

    const auto it = std::ranges::find_if(m_Joints, [sourceNode](const SkeletonJoint& joint)
    {
        return joint.SourceNode == sourceNode;
    });
    KBRAssert(it != m_Joints.end(), "Invalid source node: {}", sourceNode);
    return *it;
}

}