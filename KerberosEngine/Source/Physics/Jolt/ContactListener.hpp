#pragma once

#include "Core/Core.hpp"
#include "Physics/CollisionEvent.hpp"
#include "Physics/Jolt/Utils.hpp"

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/ContactListener.h"
#include "Jolt/Physics/Body/Body.h"

#include <mutex>

namespace Kerberos::Physics
{
	class ContactListener final : public JPH::ContactListener
	{
	public:
        JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1,
                                              const JPH::Body& inBody2,
                                              JPH::RVec3Arg inBaseOffset,
                                              const JPH::CollideShapeResult& inCollisionResult) override;

		void OnContactAdded(const JPH::Body& inBody1,
                            const JPH::Body& inBody2,
                            const JPH::ContactManifold& inManifold,
                            JPH::ContactSettings& ioSettings) override;

		void OnContactPersisted(const JPH::Body& inBody1,
                                const JPH::Body& inBody2,
                                const JPH::ContactManifold& inManifold,
                                JPH::ContactSettings& ioSettings) override;

		void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

		std::vector<CollisionEvent> GetCollisionEventsAndResetQueue();

		UUID GetAndCacheEntityID(const JPH::Body& body);

	private:
		std::vector<CollisionEvent> m_CollisionEvents;
		std::mutex m_CollisionEventsMutex;

		std::unordered_map<JPH::BodyID, UUID> m_BodyIDToEntityID;
		std::mutex m_BodyIDToEntityIDMutex;
	};
}