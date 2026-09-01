#pragma once

#include "Core/Core.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/Body.h>

namespace Kerberos::Physics
{
	class BodyActivationListener final : public JPH::BodyActivationListener
	{
	public:
        void OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override;
		void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override;
	};
}