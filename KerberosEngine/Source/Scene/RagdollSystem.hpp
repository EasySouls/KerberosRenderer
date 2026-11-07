#pragma once

#include "Core/UUID.hpp"
#include "Physics/PhysicsSystem.hpp"
#include "Renderer/Animation/AnimationEvaluator.hpp"
#include "Renderer/Animation/Skeleton.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Kerberos
{
	class Scene;
	enum class RagdollState : std::uint8_t;

	/// Owns the physics proxies and constraints for every RagdollComponent in a scene.
	///
	/// While animated, one kinematic body per joint follows the authoritative animation pose.
	/// Entering the ragdoll creates swing-twist constraints from the current pose, transfers the
	/// animation velocities into the dynamic bodies, and blends the simulated pose back into the
	/// animation runtime's FinalPose. Leaving the ragdoll blends back to animation and returns the
	/// proxies to kinematic motion.
	class RagdollSystem
	{
	public:
		explicit RagdollSystem(Scene& scene);

		/// Clears runtime state without touching the physics backend (safe before Initialize).
		void Reset();

		/// Destroys every body and constraint. Call before the physics system is cleaned up.
		void Cleanup();

		/// Creates/syncs bodies and advances the blend state. Must run after animation evaluation
		/// and before PhysicsSystem::Update.
		void PreparePhysics(float deltaTime);

		/// Reads simulated body transforms and blends them into the animation runtime's FinalPose.
		/// Must run after PhysicsSystem::Update and before AnimationRuntime::FinalizePose.
		void ReadPhysicsPose(float deltaTime);

		void EnableRagdoll(UUID entity);
		void DisableRagdoll(UUID entity);

		bool IsRagdoll(UUID entity) const;
		RagdollState GetState(UUID entity) const;
		float GetBlendWeight(UUID entity) const;
		std::uint32_t GetBodyCount(UUID entity) const;

		/// Returns the runtime transform of a joint's physics proxy. 
		/// Returns false when the ragdoll has no proxy for that joint.
		/// Useful for debug drawing and tests.
		bool TryGetJointBodyTransform(UUID entity, int32_t jointIndex,
		                              glm::vec3& outPosition, glm::quat& outRotation) const;

		/// Returns the joint transform derived from its proxy body (body transform plus the
		/// captured joint offset). This is the world-space joint position/orientation that the
		/// proxy represents, independent of the rigid capsule's length.
		bool TryGetJointWorldTransform(UUID entity, int32_t jointIndex,
		                               glm::vec3& outPosition, glm::quat& outRotation) const;

	private:
		struct Instance
		{
			UUID Entity = UUID::Invalid();
			UUID SkeletonEntity = UUID::Invalid();
			Ref<Skeleton> Skeleton = nullptr;

			RagdollState State = static_cast<RagdollState>(0);
			float BlendWeight = 0.0f;
			float BlendDuration = 0.25f;
			float JointRadiusScale = 1.0f;
			std::uint32_t CollisionGroup = 0;
			bool StartInRagdoll = false;
			bool Initialized = false;
			bool CreationFailed = false;
			bool ConstraintsCreated = false;

			// One kinematic/dynamic proxy per joint, indexed by skeleton joint index.
			std::vector<PhysicsBodyHandle> JointBodies;
			std::vector<PhysicsConstraintHandle> JointConstraints;

			// Joint transform relative to its proxy body, captured at creation.
			std::vector<glm::vec3> JointLocalPositions;
			std::vector<glm::quat> JointLocalRotations;

			// Scratch buffers reused every frame to avoid allocations.
			std::vector<glm::vec3> TargetBodyPositions;
			std::vector<glm::quat> TargetBodyRotations;
			std::vector<glm::vec3> PreviousBodyPositions;
			std::vector<glm::quat> PreviousBodyRotations;
			std::vector<JointPose> PhysicsLocalPose;
			std::vector<glm::vec3> ModelJointPositions;
			std::vector<glm::quat> ModelJointRotations;
			std::vector<glm::vec3> JointWorldPositions;
			std::vector<glm::quat> JointWorldRotations;
		};

		void SyncInstances();
		bool CreateRagdoll(Instance& instance);
		void DestroyRagdoll(Instance& instance);
		bool ComputeBodyTargets(Instance& instance) const;
		void SyncKinematicBodies(Instance& instance, float deltaTime) const;
		void CreateConstraints(Instance& instance) const;
		void DestroyConstraints(Instance& instance) const;
		void BeginRagdoll(Instance& instance, float deltaTime) const;
		void FinishRecovery(Instance& instance) const;
		void UpdateJointWorldsFromBodies(Instance& instance) const;

		PhysicsSystem& GetPhysics() const;
		Instance* FindInstance(UUID entity);
		const Instance* FindInstance(UUID entity) const;

	private:
		Scene* m_Scene = nullptr;

		std::unordered_map<UUID, Instance> m_Instances;
		std::unordered_set<UUID> m_PendingEnable;
		std::unordered_set<UUID> m_PendingDisable;
		std::unordered_set<UUID> m_Warned;
		std::vector<UUID> m_LiveEntities;

		std::uint32_t m_NextCollisionGroup = 1;
	};
}
