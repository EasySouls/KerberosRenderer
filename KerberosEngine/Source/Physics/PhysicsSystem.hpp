#pragma once

#include "Core/Core.hpp"
#include "Core/UUID.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Kerberos
{
	class Scene;

	struct RaycastHit
	{
		glm::vec3 Point;
		glm::vec3 Normal;
		float Distance;
		UUID EntityID;
	};

	class PhysicsSystem
	{
	public:
		PhysicsSystem() = default;
		virtual ~PhysicsSystem() = default;

		PhysicsSystem(const PhysicsSystem& other) = default;
		PhysicsSystem(PhysicsSystem&& other) noexcept = delete;

		PhysicsSystem& operator=(const PhysicsSystem& other) = delete;
		PhysicsSystem& operator=(PhysicsSystem&& other) noexcept = delete;

		virtual void Initialize(const Ref<Scene>& scene) = 0;
		virtual void Cleanup() = 0;

		virtual void Update(float deltaTime) = 0;

		/**
		 * Applies an impulse to the center of mass of the body 
		 * @param bodyId The ID of the body to apply the impulse to
		 * @param impulse The impulse to apply to the body
		 */
		virtual void AddImpulse(std::uint32_t bodyId, const glm::vec3& impulse) const = 0;

		/**
		 * Applies an impulse to the world position on the body, which may not be the center of mass,
		 * so it can cause both linear and angular velocity changes.
		 * @param bodyId The ID of the body to apply the impulse to
		 * @param impulse The impulse to apply to the body
		 * @param point The world position on the body to apply the impulse to
		 */
		virtual void AddImpulse(std::uint32_t bodyId, const glm::vec3& impulse, const glm::vec3& point) const = 0;

		/**
		 * Sets the linear velocity of the body, which will overwrite any existing velocity.
		 * @param bodyId The ID of the body to set the velocity of
		 * @param velocity The new linear velocity of the body
		 */
		virtual void SetLinearVelocity(std::uint32_t bodyId, const glm::vec3& velocity) const = 0;

		/**
		* Performs a raycast query from the given origin in the specified direction.
		* @param origin The world-space starting point of the ray.
		* @param direction The world-space direction of the ray.
		* @param maxDistance The maximum distance to check for collisions.
		* @param outHit The hit information if a collision is found.
		* @return True if a collision was found, false otherwise.
		*/
		virtual bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit) const = 0;
	};
}