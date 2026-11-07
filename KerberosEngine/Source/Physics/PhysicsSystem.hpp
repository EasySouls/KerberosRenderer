#pragma once

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "CollisionEvent.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

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

	enum class PhysicsMotionType : std::uint8_t
	{
		Static,
		Kinematic,
		Dynamic
	};

	enum class PhysicsShapeType : std::uint8_t
	{
		Box,
		Sphere,
		Capsule
	};

	struct PhysicsShapeDesc
	{
		PhysicsShapeType Type = PhysicsShapeType::Capsule;
		glm::vec3 HalfExtents = glm::vec3(0.5f);
		float Radius = 0.1f;
		/// Half height of the cylindrical section of a capsule (excludes the caps).
		float HalfHeight = 0.5f;
	};

	/// Opaque runtime body handle. The value is the engine's stable body id and can be passed to
	/// the existing velocity/impulse APIs.
	using PhysicsBodyHandle = std::uint32_t;
	using PhysicsConstraintHandle = std::uint64_t;

	struct PhysicsBodyDesc
	{
		PhysicsShapeDesc Shape;
		glm::vec3 Position = glm::vec3(0.0f);
		glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		PhysicsMotionType MotionType = PhysicsMotionType::Dynamic;
		float Mass = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.1f;
		bool UseGravity = true;
		bool AllowSleeping = true;
		/// Bodies in the same non-invalid group use the engine's self-collision filter. Different
		/// groups collide normally, as do grouped bodies against ungrouped bodies.
		std::uint32_t CollisionGroup = 0xFFFFFFFFu;
		std::uint32_t CollisionSubGroup = 0;

		/// Optional owning entity for raycast/collision attribution. Standalone bodies without an
		/// owner are still created but are not returned by entity raycasts.
		UUID UserData = UUID::Invalid();
	};

	/// Swing-twist constraint description. Positions and axes are in world space at creation time
	/// and are converted to body-local space by the physics backend.
	struct PhysicsConstraintDesc
	{
		PhysicsBodyHandle BodyA = 0;
		PhysicsBodyHandle BodyB = 0;
		glm::vec3 Position = glm::vec3(0.0f);
		glm::vec3 TwistAxis = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 PlaneAxis = glm::vec3(1.0f, 0.0f, 0.0f);
		float SwingLimit = 0.6f;
		float TwistMin = -0.6f;
		float TwistMax = 0.6f;
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
		 * Creates a standalone body that is not owned by an ECS RigidBody3DComponent. Used by
		 * systems such as the ragdoll, which manage their own body lifetime.
		 * @return A handle that can be passed to DestroyBody and the transform/velocity APIs.
		 */
		virtual PhysicsBodyHandle CreateBody(const PhysicsBodyDesc& desc) = 0;

		/// Removes and destroys a body created with CreateBody.
		virtual void DestroyBody(PhysicsBodyHandle body) = 0;

		/// Teleports a body to the given world transform.
		virtual void SetBodyTransform(PhysicsBodyHandle body,
		                              const glm::vec3& position,
		                              const glm::quat& rotation) = 0;

		/// Moves a kinematic body toward the target transform. A non-positive delta time
		/// teleports instead, which is useful for spawning and large changes.
		virtual void MoveKinematicBody(PhysicsBodyHandle body,
		                               const glm::vec3& position,
		                               const glm::quat& rotation,
		                               float deltaTime) = 0;

		virtual void GetBodyTransform(PhysicsBodyHandle body,
		                              glm::vec3& outPosition,
		                              glm::quat& outRotation) const = 0;

		virtual void SetBodyMotionType(PhysicsBodyHandle body, PhysicsMotionType type) = 0;

		/**
		 * Creates a swing-twist constraint between two bodies. Returns an invalid handle on
		 * failure. Constraints are owned by the physics system until DestroyConstraint.
		 */
		virtual PhysicsConstraintHandle CreateConstraint(const PhysicsConstraintDesc& desc) = 0;
		virtual void DestroyConstraint(PhysicsConstraintHandle constraint) = 0;

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
		 * Sets the angular velocity of the body, overwriting any existing angular velocity.
		 */
		virtual void SetAngularVelocity(std::uint32_t bodyId, const glm::vec3& velocity) const = 0;

		virtual glm::vec3 GetLinearVelocity(std::uint32_t bodyId) const = 0;
		virtual glm::vec3 GetAngularVelocity(std::uint32_t bodyId) const = 0;

		/**
		* Performs a raycast query from the given origin in the specified direction.
		* @param origin The world-space starting point of the ray.
		* @param direction The world-space direction of the ray.
		* @param maxDistance The maximum distance to check for collisions.
		* @param outHit The hit information if a collision is found.
		* @return True if a collision was found, false otherwise.
		*/
		virtual bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit) const = 0;

		/**
		* Returns a list of collision events that occurred during the last physics update.
		*/
		virtual std::vector<CollisionEvent> GetCollisionEvents() const = 0;
	};
}