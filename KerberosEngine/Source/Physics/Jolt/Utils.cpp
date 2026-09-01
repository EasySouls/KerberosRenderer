#include "Utils.hpp"
#include "Profiling/Instrumentor.hpp"

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

import Kerberos;

namespace Kerberos::Physics::Utils {

JPH::EMotionType GetJPHMotionTypeFromComponent(const RigidBody3DComponent& rb)
{
    switch (rb.Type) {
    case RigidBody3DComponent::BodyType::Static:
        return JPH::EMotionType::Static;
    case RigidBody3DComponent::BodyType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case RigidBody3DComponent::BodyType::Dynamic:
        return JPH::EMotionType::Dynamic;
    }

    KBRAssert(false, "Unknown body type!");
    return JPH::EMotionType::Static;
}

JPH::Array<JPH::Vec3> KBRVerticesToJoltVertices(const std::vector<Vertex>& vertices)
{
    JPH::Array<JPH::Vec3> joltVertices;
    joltVertices.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        joltVertices.emplace_back(vertex.Position.x, vertex.Position.y, vertex.Position.z);
    }
    return joltVertices;
}

JPH::ObjectLayer GetObjectLayerFromComponent(const RigidBody3DComponent& rb)
{
    switch (rb.Type) {
    case RigidBody3DComponent::BodyType::Static:
        return Layers::NON_MOVING;
    case RigidBody3DComponent::BodyType::Kinematic:
    case RigidBody3DComponent::BodyType::Dynamic:
        return Layers::MOVING;
    }

    KBRAssert(false, "Unknown body type!");
    return Layers::NON_MOVING;
}

 void ApplyJoltTransformToEntity(glm::mat4& worldTransform, const JPH::Body& body, TransformComponent& tc)
{
    KBR_PROFILE_FUNCTION();

    /// TODO: Update the transform, rotation and scale of the entity, not its world transform

    const JPH::RVec3 joltPosition = body.GetPosition();
    const JPH::Quat joltRotation = body.GetRotation();

    glm::vec3 position = ToGlmVec3(joltPosition);
    glm::quat rotation = ToGlmQuat(joltRotation);

    /// Decompose the current transform to get the scale
    glm::vec3 scale, skew;
    glm::vec4 perspective;
    glm::quat oldRotation;
    glm::vec3 oldPosition;

    glm::decompose(worldTransform, scale, oldRotation, oldPosition, skew, perspective);

    /// Rebuild world transform using physics position & rotation but keep original scale
    glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotationMatrix = glm::toMat4(rotation);
    glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);

    worldTransform = translationMatrix * rotationMatrix * scaleMatrix;
    tc.Translation = position;
    tc.Rotation = glm::eulerAngles(rotation);
    tc.Scale = scale;
}

JPH::Ref<JPH::Shape> CreateJoltMeshShape(const Ref<Mesh>& mesh, const std::string_view debugName)
{
    const std::vector<Vertex>& kerberosVertices = mesh->GetVertices();
    const std::vector<uint32_t>& kerberosIndices = mesh->GetIndices();

    JPH::VertexList jphVertices;
    jphVertices.reserve(kerberosVertices.size());
    for (const auto& vertex : kerberosVertices) {
        jphVertices.push_back(JPH::Float3(vertex.Position.x, vertex.Position.y, vertex.Position.z));
    }

    JPH::IndexedTriangleList jphTriangles;
    KBRAssert(kerberosIndices.size() % 3 == 0, "Mesh indices must form triangles!");

    jphTriangles.reserve(kerberosIndices.size() / 3);
    for (size_t i = 0; i < kerberosIndices.size(); i += 3) {
        jphTriangles.push_back(
            JPH::IndexedTriangle(kerberosIndices[i], kerberosIndices[i + 1], kerberosIndices[i + 2]));
    }

    const JPH::MeshShapeSettings meshShapeSettings(jphVertices, jphTriangles);

    // meshShapeSettings.mBuildFaceQueryTree = true; // Often true for  meshes
    // meshShapeSettings.mMaxTrianglesPerLeaf = 8;   // Adjust as needed

    const JPH::ShapeSettings::ShapeResult shapeResult = meshShapeSettings.Create();
    if (shapeResult.HasError()) {
        Log::CoreError("Jolt: shape error on entity {}: {}", debugName, shapeResult.GetError().c_str());
        return nullptr;
    }

    return shapeResult.Get();
}

}