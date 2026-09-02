#include "ContactListener.hpp"

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/ContactListener.h"
#include "Jolt/Physics/Body/Body.h"

import Kerberos;

namespace Kerberos::Physics {

JPH::ValidateResult ContactListener::OnContactValidate(const JPH::Body& inBody1,
                                      const JPH::Body& inBody2,
                                      JPH::RVec3Arg /*inBaseOffset*/,
                                      const JPH::CollideShapeResult& /*inCollisionResult*/)
{
    Log::CoreTrace("Contact validate callback: {} - {}", inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex());

    /// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

void ContactListener::OnContactAdded(const JPH::Body& inBody1,
                    const JPH::Body& inBody2,
                    const JPH::ContactManifold& inManifold,
                    JPH::ContactSettings& /*ioSettings*/)
{
    Log::CoreTrace("A contact was added: {} - {}", inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex());

    const JPH::Vec3& joltContactPoint = inManifold.GetWorldSpaceContactPointOn1(0);
    const glm::vec3 contactPoint = Utils::ToGlmVec3(joltContactPoint);

    const glm::vec3 worldSpaceNormal = Utils::ToGlmVec3(inManifold.mWorldSpaceNormal);

    const float penetrationDepth = inManifold.mPenetrationDepth;

    std::scoped_lock<std::mutex> lock(m_CollisionEventsMutex);
    m_CollisionEvents.push_back({ .EntityA = GetAndCacheEntityID(inBody1),
                                  .EntityB = GetAndCacheEntityID(inBody2),
                                  .ContactPoint = contactPoint,
                                  .ContactNormal = worldSpaceNormal,
                                  .PenetrationDepth = penetrationDepth,
                                  .EventType = CollisionEventType::Enter });
}

void ContactListener::OnContactPersisted(const JPH::Body& inBody1,
                        const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings& /*ioSettings*/)
{
    Log::CoreTrace("A contact was persisted: {} - {}", inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex());

    const JPH::Vec3& joltContactPoint = inManifold.GetWorldSpaceContactPointOn1(0);
    const glm::vec3 contactPoint = Utils::ToGlmVec3(joltContactPoint);

    const glm::vec3 worldSpaceNormal = Utils::ToGlmVec3(inManifold.mWorldSpaceNormal);

    const float penetrationDepth = inManifold.mPenetrationDepth;

    std::scoped_lock<std::mutex> lock(m_CollisionEventsMutex);
    m_CollisionEvents.push_back({ .EntityA = GetAndCacheEntityID(inBody1),
                                  .EntityB = GetAndCacheEntityID(inBody2),
                                  .ContactPoint = contactPoint,
                                  .ContactNormal = worldSpaceNormal,
                                  .PenetrationDepth = penetrationDepth,
                                  .EventType = CollisionEventType::Persist });
}

void ContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
{
    Log::CoreTrace("A contact was removed: {} - {}",
                             inSubShapePair.GetBody1ID().GetIndex(),
                             inSubShapePair.GetBody2ID().GetIndex());

    const UUID entityA = m_BodyIDToEntityID.at(inSubShapePair.GetBody1ID());
    const UUID entityB = m_BodyIDToEntityID.at(inSubShapePair.GetBody2ID());

    std::scoped_lock<std::mutex> lock(m_CollisionEventsMutex);
    m_CollisionEvents.push_back({ .EntityA = entityA,
                                  .EntityB = entityB,
                                  .ContactPoint = glm::vec3{ 0.0f },
                                  .ContactNormal = glm::vec3{ 0.0f },
                                  .PenetrationDepth = 0.0f,
                                  .EventType = CollisionEventType::Exit });
}

std::vector<CollisionEvent> ContactListener::GetCollisionEventsAndResetQueue()
{
    std::scoped_lock lock(m_CollisionEventsMutex);
    std::vector<CollisionEvent> events = m_CollisionEvents;
    m_CollisionEvents.clear();
    return events;
}

UUID ContactListener::GetAndCacheEntityID(const JPH::Body& body)
{
    const uint64_t entityId = body.GetUserData();
    KBRAssert(entityId != 0, "Entity ID has not been set on JPH::Body!");

    if (entityId == 0) {
        return UUID::Invalid();
    }

    {
        std::scoped_lock lock(m_BodyIDToEntityIDMutex);
        m_BodyIDToEntityID[body.GetID()] = UUID(entityId);
    }

    return UUID(entityId);
}

}