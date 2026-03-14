#pragma once

#include "Core/Core.hpp"

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/ContactListener.h"
#include "Jolt/Physics/Body/Body.h"

namespace Kerberos::Physics
{
	class ContactListener final : public JPH::ContactListener
	{
	public:
		JPH::ValidateResult	OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
		{
			KBR_CORE_TRACE("Contact validate callback: {} - {}", inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex());

			/// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
		{
			KBR_CORE_TRACE("A contact was added: {} - {}", inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex());
		}

		void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
		{
			KBR_CORE_TRACE("A contact was persisted: {} - {}", inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex());
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
		{
			KBR_CORE_TRACE("A contact was removed: {} - {}", inSubShapePair.GetBody1ID().GetIndex(), inSubShapePair.GetBody2ID().GetIndex());
		}
	};
}