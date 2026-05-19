#pragma once

#include "Core/UUID.hpp"
#include <glm/vec3.hpp>

#include <cstdint>

namespace Kerberos
{
	enum class CollisionEventType : std::uint8_t
	{
		Enter,
		Persist,
		Exit
	};

	struct CollisionEvent
	{
		UUID EntityA;
		UUID EntityB;
		glm::vec3 ContactPoint;
		glm::vec3 ContactNormal;
		float PenetrationDepth;
		CollisionEventType EventType;
	};
}