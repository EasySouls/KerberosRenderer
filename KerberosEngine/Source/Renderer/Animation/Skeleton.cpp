#include "Skeleton.hpp"

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
    }
}

const SkeletonJoint& Skeleton::GetJointBySourceNode(int32_t sourceNode) const
{
    KBRAssert(sourceNode >= 0 && sourceNode < static_cast<int32_t>(m_Joints.size()), "Invalid source node: {}", sourceNode);

    return std::ranges::find_if(m_Joints, [sourceNode](const SkeletonJoint& joint) { return joint.SourceNode == sourceNode; }).operator*();
}

}