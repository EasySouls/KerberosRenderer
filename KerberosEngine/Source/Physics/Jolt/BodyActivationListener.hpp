#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/Body.h>

namespace Kerberos::Physics
{
	class BodyActivationListener final : public JPH::BodyActivationListener
	{
	public:
		void OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override
		{
			KBR_CORE_TRACE("A body got activated: {}", inBodyID.GetIndex());
		}

		void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override
		{
			KBR_CORE_TRACE("A body went to sleep: {}", inBodyID.GetIndex());

		}
	};
}