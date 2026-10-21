#pragma once

#include "Layers.hpp"
#include "Core/Core.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Vertex.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/PhysicsComponents.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Array.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Body/Body.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>


namespace Kerberos::Physics::Utils {

JPH::EMotionType GetJPHMotionTypeFromComponent(const RigidBody3DComponent& rb);

JPH::Array<JPH::Vec3> KBRVerticesToJoltVertices(const std::vector<Vertex>& vertices);

JPH::ObjectLayer GetObjectLayerFromComponent(const RigidBody3DComponent& rb);


/**
 * Applies the Jolt physics transform to the entity's world transform.
 * @param worldTransform The world transform of the entity to update.
 * @param body The Jolt body to get the position and rotation from.
 * @param tc The TransformComponent of the entity to update.
 */
void ApplyJoltTransformToEntity(glm::mat4& worldTransform, const JPH::Body& body, TransformComponent& tc);

JPH::Ref<JPH::Shape> CreateJoltMeshShape(const Ref<Mesh>& mesh, std::string_view debugName);

inline glm::vec3 ToGlmVec3(const JPH::RVec3& v)
{
    return {
        (v.GetX()),
        (v.GetY()),
        (v.GetZ())
    };
}

inline glm::quat ToGlmQuat(const JPH::Quat& q)
{
    return {
        (q.GetW()),
        (q.GetX()),
        (q.GetY()),
        (q.GetZ())
    };
}

}