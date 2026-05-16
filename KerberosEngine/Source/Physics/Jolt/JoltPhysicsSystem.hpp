#pragma once

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Physics/PhysicsSystem.hpp"

#include <memory>
#include <unordered_map>

namespace JPH
{
	class PhysicsSystem;
	class BodyInterface;
	class TempAllocator;
	class JobSystem;
	class Shape;
	class Body;

	template <class T>
	class RefConst;

	class ObjectVsBroadPhaseLayerFilter;
	class BroadPhaseLayerInterface;
	class ContactListener;
	class BodyActivationListener;
	class ObjectLayerPairFilter;

	class Vec3;
}

namespace Kerberos
{
	class UUID;
	class Entity;
	class Scene;

	class JoltPhysicsSystem : public PhysicsSystem
	{
	public:
		JoltPhysicsSystem() = default;
		~JoltPhysicsSystem() override;
		JoltPhysicsSystem(const JoltPhysicsSystem& other) = default;
		JoltPhysicsSystem(JoltPhysicsSystem&& other) noexcept = delete;
		JoltPhysicsSystem& operator=(const JoltPhysicsSystem& other) = delete;
		JoltPhysicsSystem& operator=(JoltPhysicsSystem&& other) noexcept = delete;

		void Initialize(const Ref<Scene>& scene) override;
		void Update(float deltaTime) override;

		void AddImpulse(uint32_t bodyId, const glm::vec3& impulse) const override;
		void AddImpulse(uint32_t bodyId, const glm::vec3& impulse, const glm::vec3& point) const override;

		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
			RaycastHit& outHit) const override;

		/// TODO: Might not be a good idea to expose this
		JPH::BodyInterface& GetBodyInterface() const;

		void Cleanup() override;

	private:
		void UpdateAndCreatePhysicsBodies();
		void CreatePhysicsBody(const Entity& entity);
		JPH::RefConst<JPH::Shape> CreateShapeForEntity(const Entity& entity);

		void SyncTransforms() const;

		static bool IsColliderTrigger(const Entity& entity);

	private:
		std::weak_ptr<Scene> m_Scene;

		std::unordered_map<UUID, JPH::Vec3> m_ColliderOffsets;

		JPH::PhysicsSystem* m_JoltSystem = nullptr;
		JPH::TempAllocator* m_PhysicsTempAllocator = nullptr;
		JPH::JobSystem* m_PhysicsJobSystem = nullptr;
		JPH::ObjectVsBroadPhaseLayerFilter* m_ObjectVsBroadPhaseLayerFilter = nullptr;
		JPH::BroadPhaseLayerInterface* m_BroadPhaseLayerInterface = nullptr;
		JPH::ObjectLayerPairFilter* m_ObjectVsObjectLayerFilter = nullptr;
		JPH::ContactListener* m_ContactListener = nullptr;
		JPH::BodyActivationListener* m_BodyActivationListener = nullptr;

		/**
		* Mapping from Jolt body ID to entity UUID. 
		* This is needed to be able to get the entity associated with a physics body when we get a collision callback from Jolt.
		*/
		std::unordered_map<uint32_t, UUID> m_BodyIDToEntityID;

		/**
		 * Shape cache to avoid creating the same shape multiple times.
		 */
		std::unordered_map<size_t, JPH::RefConst<JPH::Shape>> m_ShapeCache;
	};

}