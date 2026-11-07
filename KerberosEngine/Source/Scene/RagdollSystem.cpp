#include "RagdollSystem.hpp"
#include "Scene.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"

#include "Profiling/Profilers.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

import Kerberos;

namespace Kerberos
{
	namespace
	{
		// Must match JoltPhysicsSystem's self-collision filter subgroup capacity.
		constexpr std::uint32_t cMaxRagdollJoints = 256;

		glm::quat RotationFromMatrix(const glm::mat4& matrix)
		{
			glm::mat3 rotation(matrix);
			const float scaleX = glm::length(rotation[0]);
			const float scaleY = glm::length(rotation[1]);
			const float scaleZ = glm::length(rotation[2]);

			if (scaleX > 1.0e-6f) rotation[0] /= scaleX;
			if (scaleY > 1.0e-6f) rotation[1] /= scaleY;
			if (scaleZ > 1.0e-6f) rotation[2] /= scaleZ;

			const glm::quat orientation = glm::quat_cast(rotation);
			const float length = glm::length(orientation);
			return length > 1.0e-6f
				? orientation / length
				: glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		glm::quat FromToRotation(const glm::vec3& from, const glm::vec3& to)
		{
			const glm::vec3 a = glm::normalize(from);
			const glm::vec3 b = glm::normalize(to);
			const float dot = glm::dot(a, b);

			if (dot > 0.999999f)
				return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

			if (dot < -0.999999f)
			{
				glm::vec3 axis = glm::cross(a, glm::vec3(0.0f, 1.0f, 0.0f));
				if (glm::length2(axis) < 1.0e-6f)
					axis = glm::cross(a, glm::vec3(1.0f, 0.0f, 0.0f));
				return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
			}

			const glm::vec3 axis = glm::normalize(glm::cross(a, b));
			return glm::normalize(glm::angleAxis(std::acos(dot), axis));
		}

		void JointLimitsForName(const std::string& name, float& outSwing, float& outTwist)
		{
			std::string lower;
			lower.reserve(name.size());
			for (const char character : name)
				lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));

			if (lower.find("neck") != std::string::npos || lower.find("head") != std::string::npos)
			{
				outSwing = 0.35f;
				outTwist = 0.5f;
			}
			else if (lower.find("shoulder") != std::string::npos || lower.find("hip") != std::string::npos)
			{
				outSwing = 0.9f;
				outTwist = 0.7f;
			}
			else if (lower.find("elbow") != std::string::npos || lower.find("knee") != std::string::npos)
			{
				outSwing = 0.4f;
				outTwist = 0.15f;
			}
			else
			{
				outSwing = 0.6f;
				outTwist = 0.5f;
			}
		}

		float AdvanceBlendWeight(const float weight, const float duration, const float deltaTime,
		                         const float direction)
		{
			if (duration <= 0.0f)
				return direction > 0.0f ? 1.0f : 0.0f;

			if (deltaTime <= 0.0f)
				return weight;

			return std::clamp(weight + direction * deltaTime / duration, 0.0f, 1.0f);
		}
	}

	RagdollSystem::RagdollSystem(Scene& scene)
		: m_Scene(&scene)
	{
	}

	PhysicsSystem& RagdollSystem::GetPhysics() const
	{
		return m_Scene->GetPhysicsSystem();
	}

	RagdollSystem::Instance* RagdollSystem::FindInstance(const UUID entity)
	{
		const auto it = m_Instances.find(entity);
		return it != m_Instances.end() ? &it->second : nullptr;
	}

	const RagdollSystem::Instance* RagdollSystem::FindInstance(const UUID entity) const
	{
		const auto it = m_Instances.find(entity);
		return it != m_Instances.end() ? &it->second : nullptr;
	}

	void RagdollSystem::Reset()
	{
		// The caller owns the physics lifetime: Reset is used after the physics backend has been
		// reinitialized, so the old handles must not be dereferenced. Cleanup destroys bodies and
		// must run before PhysicsSystem::Cleanup.
		m_Instances.clear();
		m_PendingEnable.clear();
		m_PendingDisable.clear();
		m_Warned.clear();
		m_NextCollisionGroup = 1;
	}

	void RagdollSystem::Cleanup()
	{
		for (auto& instance : m_Instances | std::views::values)
			DestroyRagdoll(instance);

		m_Instances.clear();
	}

	void RagdollSystem::EnableRagdoll(const UUID entity)
	{
		m_PendingDisable.erase(entity);
		m_PendingEnable.insert(entity);
	}

	void RagdollSystem::DisableRagdoll(const UUID entity)
	{
		m_PendingEnable.erase(entity);
		m_PendingDisable.insert(entity);
	}

	bool RagdollSystem::IsRagdoll(const UUID entity) const
	{
		const Instance* instance = FindInstance(entity);
		return instance && instance->State != RagdollState::Animated;
	}

	RagdollState RagdollSystem::GetState(const UUID entity) const
	{
		const Instance* instance = FindInstance(entity);
		return instance ? instance->State : RagdollState::Animated;
	}

	float RagdollSystem::GetBlendWeight(const UUID entity) const
	{
		const Instance* instance = FindInstance(entity);
		return instance ? instance->BlendWeight : 0.0f;
	}

	std::uint32_t RagdollSystem::GetBodyCount(const UUID entity) const
	{
		const Instance* instance = FindInstance(entity);
		if (!instance || !instance->Initialized)
			return 0;
		return static_cast<std::uint32_t>(instance->JointBodies.size());
	}

	bool RagdollSystem::TryGetJointBodyTransform(const UUID entity, const int32_t jointIndex,
	                                             glm::vec3& outPosition,
	                                             glm::quat& outRotation) const
	{
		const Instance* instance = FindInstance(entity);
		if (!instance || !instance->Initialized || jointIndex < 0 ||
		    static_cast<size_t>(jointIndex) >= instance->JointBodies.size())
		{
			return false;
		}

		const PhysicsBodyHandle body = instance->JointBodies[static_cast<size_t>(jointIndex)];
		if (body == 0)
			return false;

		GetPhysics().GetBodyTransform(body, outPosition, outRotation);
		return true;
	}

	bool RagdollSystem::TryGetJointWorldTransform(const UUID entity, const int32_t jointIndex,
	                                              glm::vec3& outPosition,
	                                              glm::quat& outRotation) const
	{
		glm::vec3 bodyPosition;
		glm::quat bodyRotation;
		if (!TryGetJointBodyTransform(entity, jointIndex, bodyPosition, bodyRotation))
			return false;

		const Instance* instance = FindInstance(entity);
		const size_t index = static_cast<size_t>(jointIndex);
		outPosition = bodyPosition + bodyRotation * instance->JointLocalPositions[index];
		outRotation = bodyRotation * instance->JointLocalRotations[index];
		return true;
	}

	void RagdollSystem::SyncInstances()
	{
		KBR_TRACY_FUNCTION();

		if (!m_Scene)
			return;

		m_LiveEntities.clear();

		const auto view = m_Scene->m_Registry.view<RagdollComponent>();
		for (const auto entityId : view)
		{
			const auto& ragdoll = view.get<RagdollComponent>(entityId);
			const Entity entity(entityId, m_Scene);
			const UUID entityUUID = entity.GetUUID();

			UUID skeletonUUID = ragdoll.SkeletonEntity;
			if (skeletonUUID.IsValid())
			{
				const Entity skeletonEntity = m_Scene->TryGetEntityByUUID(skeletonUUID);
				if (!skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>())
					skeletonUUID = UUID::Invalid();
			}

			if (!skeletonUUID.IsValid() && entity.HasComponent<SkeletonComponent>())
				skeletonUUID = entityUUID;

			if (!skeletonUUID.IsValid())
			{
				if (m_Warned.insert(entityUUID).second)
				{
					Log::CoreWarn("RagdollComponent on entity {} does not target a skeleton entity",
					              entityUUID);
				}
				continue;
			}

			auto it = m_Instances.find(entityUUID);
			if (it == m_Instances.end())
			{
				Instance instance{};
				instance.Entity = entityUUID;
				instance.SkeletonEntity = skeletonUUID;
				instance.State = RagdollState::Animated;
				instance.BlendDuration = std::max(0.0f, ragdoll.BlendDuration);
				instance.JointRadiusScale = std::max(0.01f, ragdoll.JointRadiusScale);
				instance.StartInRagdoll = ragdoll.StartInRagdoll;
				instance.CollisionGroup = m_NextCollisionGroup++;
				if (m_NextCollisionGroup == 0xFFFFFFFFu)
					m_NextCollisionGroup = 1;

				it = m_Instances.emplace(entityUUID, std::move(instance)).first;
			}
			else
			{
				if (it->second.SkeletonEntity != skeletonUUID)
				{
					DestroyRagdoll(it->second);
					it->second.SkeletonEntity = skeletonUUID;
					it->second.Skeleton = nullptr;
					it->second.CollisionGroup = m_NextCollisionGroup++;
					if (m_NextCollisionGroup == 0xFFFFFFFFu)
						m_NextCollisionGroup = 1;
				}

				it->second.BlendDuration = std::max(0.0f, ragdoll.BlendDuration);
				it->second.JointRadiusScale = std::max(0.01f, ragdoll.JointRadiusScale);
			}

			m_LiveEntities.push_back(entityUUID);
		}

		for (auto it = m_Instances.begin(); it != m_Instances.end();)
		{
			if (std::ranges::find(m_LiveEntities, it->first) == m_LiveEntities.end())
			{
				DestroyRagdoll(it->second);
				it = m_Instances.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Drop requests for entities that no longer have a ragdoll (component removed/destroyed).
		for (auto it = m_PendingEnable.begin(); it != m_PendingEnable.end();)
		{
			if (m_Instances.contains(*it))
				++it;
			else
				it = m_PendingEnable.erase(it);
		}
		for (auto it = m_PendingDisable.begin(); it != m_PendingDisable.end();)
		{
			if (m_Instances.contains(*it))
				++it;
			else
				it = m_PendingDisable.erase(it);
		}
	}

	void RagdollSystem::PreparePhysics(const float deltaTime)
	{
		KBR_TRACY_FUNCTION();

		if (!m_Scene)
			return;

		SyncInstances();

		for (auto& [entity, instance] : m_Instances)
		{
			if (!instance.Initialized)
			{
				if (instance.CreationFailed || !CreateRagdoll(instance))
					continue;
			}

			const bool enableRequested = m_PendingEnable.erase(entity) > 0;
			const bool disableRequested = m_PendingDisable.erase(entity) > 0;

			if (disableRequested &&
			    (instance.State == RagdollState::Ragdoll ||
			     instance.State == RagdollState::BlendingToRagdoll))
			{
				if (instance.BlendDuration <= 0.0f)
					FinishRecovery(instance);
				else
					instance.State = RagdollState::BlendingToAnimation;
			}

			if (enableRequested)
			{
				if (instance.State == RagdollState::Animated)
				{
					if (ComputeBodyTargets(instance))
						BeginRagdoll(instance, deltaTime);
				}
				else if (instance.State == RagdollState::BlendingToAnimation)
				{
					instance.State = RagdollState::BlendingToRagdoll;
				}
			}

			switch (instance.State)
			{
			case RagdollState::Animated:
				if (ComputeBodyTargets(instance))
					SyncKinematicBodies(instance, deltaTime);
				break;

			case RagdollState::BlendingToRagdoll:
				instance.BlendWeight = AdvanceBlendWeight(
					instance.BlendWeight, instance.BlendDuration, deltaTime, 1.0f);
				if (instance.BlendWeight >= 1.0f)
				{
					instance.BlendWeight = 1.0f;
					instance.State = RagdollState::Ragdoll;
				}
				break;

			case RagdollState::Ragdoll:
				instance.BlendWeight = 1.0f;
				break;

			case RagdollState::BlendingToAnimation:
				instance.BlendWeight = AdvanceBlendWeight(
					instance.BlendWeight, instance.BlendDuration, deltaTime, -1.0f);
				if (instance.BlendWeight <= 0.0f)
					FinishRecovery(instance);
				break;
			}
		}
	}

	void RagdollSystem::ReadPhysicsPose([[maybe_unused]] const float deltaTime)
	{
		KBR_TRACY_FUNCTION();

		if (!m_Scene)
			return;

		AnimationRuntime& runtime = m_Scene->GetAnimationRuntime();

		for (auto& instance : m_Instances | std::views::values)
		{
			if (!instance.Initialized || instance.State == RagdollState::Animated ||
			    instance.BlendWeight <= 0.0f)
			{
				continue;
			}

			const SkeletonPose* finalPose = runtime.FindPose(instance.SkeletonEntity);
			glm::mat4 baseWorld;
			if (!finalPose || !runtime.TryGetSkeletonWorldTransform(instance.SkeletonEntity, baseWorld))
				continue;

			UpdateJointWorldsFromBodies(instance);

			const glm::mat4 inverseBaseWorld = glm::inverse(baseWorld);
			const glm::quat inverseBaseRotation = glm::inverse(RotationFromMatrix(baseWorld));
			const size_t jointCount = instance.JointBodies.size();

			for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
			{
				// Physics joint transforms live in world space.
				// The pose is expressed in the skeleton's model space. 
				// Convert once, then build local TRS relative to the
				// parent joint without a matrix inverse per joint.
				const glm::vec3 modelPosition = glm::vec3(
					inverseBaseWorld * glm::vec4(instance.JointWorldPositions[jointIndex], 1.0f));
				const glm::quat modelRotation =
					inverseBaseRotation * instance.JointWorldRotations[jointIndex];

				instance.ModelJointPositions[jointIndex] = modelPosition;
				instance.ModelJointRotations[jointIndex] = modelRotation;

				const int32_t parent = instance.Skeleton
					? instance.Skeleton->GetJoints()[jointIndex].Parent
					: -1;

				JointPose& local = instance.PhysicsLocalPose[jointIndex];
				if (parent >= 0 && static_cast<size_t>(parent) < jointIndex)
				{
					const glm::quat inverseParent =
						glm::inverse(instance.ModelJointRotations[static_cast<size_t>(parent)]);
					local.Translation = inverseParent *
						(modelPosition - instance.ModelJointPositions[static_cast<size_t>(parent)]);
					local.Rotation = inverseParent * modelRotation;
				}
				else
				{
					local.Translation = modelPosition;
					local.Rotation = modelRotation;
				}

				local.Scale = jointIndex < finalPose->Local.size()
					? finalPose->Local[jointIndex].Scale
					: glm::vec3(1.0f);
			}

			runtime.ApplyRagdollPose(instance.SkeletonEntity, instance.PhysicsLocalPose,
			                         instance.BlendWeight);
		}
	}

	bool RagdollSystem::CreateRagdoll(Instance& instance)
	{
		KBR_TRACY_FUNCTION();

		if (!m_Scene)
			return false;

		const Entity skeletonEntity = m_Scene->TryGetEntityByUUID(instance.SkeletonEntity);
		if (!skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>())
			return false;

		const auto& skeletonComponent = skeletonEntity.GetComponent<SkeletonComponent>();
		if (!skeletonComponent.Skeleton)
			return false;

		AnimationRuntime& runtime = m_Scene->GetAnimationRuntime();
		const SkeletonPose* pose = runtime.FindPose(instance.SkeletonEntity);
		glm::mat4 baseWorld;
		if (!pose || !runtime.TryGetSkeletonWorldTransform(instance.SkeletonEntity, baseWorld))
			return false;

		const auto& joints = skeletonComponent.Skeleton->GetJoints();
		const size_t jointCount = joints.size();
		if (jointCount == 0 || pose->Global.size() != jointCount)
			return false;

		if (jointCount > cMaxRagdollJoints)
		{
			Log::CoreError("Cannot create ragdoll for skeleton entity {}: {} joints exceeds the "
			               "self-collision filter capacity of {}",
			               instance.SkeletonEntity, jointCount, cMaxRagdollJoints);
			instance.CreationFailed = true;
			return false;
		}

		instance.Skeleton = skeletonComponent.Skeleton;
		instance.JointBodies.assign(jointCount, 0);
		instance.JointLocalPositions.resize(jointCount);
		instance.JointLocalRotations.resize(jointCount);
		instance.TargetBodyPositions.resize(jointCount);
		instance.TargetBodyRotations.resize(jointCount);
		instance.PreviousBodyPositions.resize(jointCount);
		instance.PreviousBodyRotations.resize(jointCount);
		instance.PhysicsLocalPose.resize(jointCount);
		instance.ModelJointPositions.resize(jointCount);
		instance.ModelJointRotations.resize(jointCount);
		instance.JointWorldPositions.resize(jointCount);
		instance.JointWorldRotations.resize(jointCount);
		instance.JointConstraints.assign(jointCount, 0);
		instance.ConstraintsCreated = false;

		std::vector<glm::vec3> jointWorldPositions(jointCount);
		std::vector<glm::quat> jointWorldRotations(jointCount);
		for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
		{
			const glm::mat4 world = baseWorld * pose->Global[jointIndex];
			jointWorldPositions[jointIndex] = glm::vec3(world[3]);
			jointWorldRotations[jointIndex] = RotationFromMatrix(world);
		}

		bool success = true;
		for (size_t jointIndex = 0; jointIndex < jointCount && success; ++jointIndex)
		{
			const SkeletonJoint& joint = joints[jointIndex];

			PhysicsBodyDesc desc{};
			desc.MotionType = PhysicsMotionType::Kinematic;
			desc.CollisionGroup = instance.CollisionGroup;
			desc.CollisionSubGroup = static_cast<std::uint32_t>(jointIndex);
			desc.UserData = instance.Entity;
			desc.Friction = 0.6f;
			desc.Restitution = 0.0f;
			desc.LinearDamping = 0.05f;
			desc.AngularDamping = 0.1f;
			desc.UseGravity = true;

			glm::vec3 jointLocalPosition(0.0f);
			glm::quat jointLocalRotation(1.0f, 0.0f, 0.0f, 0.0f);

			const int32_t parent = joint.Parent;
			if (parent >= 0 && static_cast<size_t>(parent) < jointCount)
			{
				const glm::vec3 from = jointWorldPositions[static_cast<size_t>(parent)];
				const glm::vec3 to = jointWorldPositions[jointIndex];
				const glm::vec3 delta = to - from;
				const float length = glm::length(delta);

				if (length > 1.0e-4f)
				{
					const glm::vec3 direction = delta / length;
					const glm::quat bodyRotation = FromToRotation(glm::vec3(0.0f, 1.0f, 0.0f), direction);
					const glm::vec3 center = (from + to) * 0.5f;
					const float radius = std::clamp(length * 0.12f * instance.JointRadiusScale, 0.02f, 0.25f);
					const float halfHeight = std::max(length * 0.5f - radius, 0.01f);

					desc.Shape.Type = PhysicsShapeType::Capsule;
					desc.Shape.Radius = radius;
					desc.Shape.HalfHeight = halfHeight;
					desc.Position = center;
					desc.Rotation = bodyRotation;
					desc.Mass = std::max(0.2f, length * 1.5f);

					jointLocalPosition = glm::inverse(bodyRotation) * (to - center);
					jointLocalRotation = glm::inverse(bodyRotation) * jointWorldRotations[jointIndex];
				}
				else
				{
					const float radius = std::clamp(0.08f * instance.JointRadiusScale, 0.02f, 0.25f);
					desc.Shape.Type = PhysicsShapeType::Sphere;
					desc.Shape.Radius = radius;
					desc.Position = jointWorldPositions[jointIndex];
					desc.Rotation = jointWorldRotations[jointIndex];
					desc.Mass = std::max(0.2f, radius * radius * 20.0f);
				}
			}
			else
			{
				float radius = 0.1f;
				for (size_t childIndex = 0; childIndex < jointCount; ++childIndex)
				{
					if (joints[childIndex].Parent == static_cast<int32_t>(jointIndex))
					{
						const float distance = glm::length(jointWorldPositions[childIndex] -
						                                   jointWorldPositions[jointIndex]);
						if (distance > 1.0e-4f)
						{
							radius = std::clamp(distance * 0.15f * instance.JointRadiusScale, 0.05f, 0.25f);
							break;
						}
					}
				}

				desc.Shape.Type = PhysicsShapeType::Sphere;
				desc.Shape.Radius = radius;
				desc.Position = jointWorldPositions[jointIndex];
				desc.Rotation = jointWorldRotations[jointIndex];
				desc.Mass = std::max(0.5f, radius * radius * 20.0f);
			}

			const PhysicsBodyHandle body = GetPhysics().CreateBody(desc);
			if (body == 0)
			{
				success = false;
				break;
			}

			instance.JointBodies[jointIndex] = body;
			instance.JointLocalPositions[jointIndex] = jointLocalPosition;
			instance.JointLocalRotations[jointIndex] = jointLocalRotation;
			instance.TargetBodyPositions[jointIndex] = desc.Position;
			instance.TargetBodyRotations[jointIndex] = desc.Rotation;
			instance.PreviousBodyPositions[jointIndex] = desc.Position;
			instance.PreviousBodyRotations[jointIndex] = desc.Rotation;
		}

		if (!success)
		{
			DestroyRagdoll(instance);
			Log::CoreWarn("Failed to create ragdoll bodies for skeleton entity {}",
			              instance.SkeletonEntity);
			return false;
		}

		instance.Initialized = true;
		instance.State = RagdollState::Animated;
		instance.BlendWeight = 0.0f;

		Log::CoreInfo("Created ragdoll for skeleton entity {} with {} joint bodies",
		              instance.SkeletonEntity, jointCount);

		if (instance.StartInRagdoll && ComputeBodyTargets(instance))
			BeginRagdoll(instance, 0.0f);

		return true;
	}

	void RagdollSystem::DestroyRagdoll(Instance& instance)
	{
		DestroyConstraints(instance);

		for (const PhysicsBodyHandle body : instance.JointBodies)
		{
			if (body != 0)
				GetPhysics().DestroyBody(body);
		}

		instance.JointBodies.clear();
		instance.JointConstraints.clear();
		instance.JointLocalPositions.clear();
		instance.JointLocalRotations.clear();
		instance.TargetBodyPositions.clear();
		instance.TargetBodyRotations.clear();
		instance.PreviousBodyPositions.clear();
		instance.PreviousBodyRotations.clear();
		instance.PhysicsLocalPose.clear();
		instance.ModelJointPositions.clear();
		instance.ModelJointRotations.clear();
		instance.JointWorldPositions.clear();
		instance.JointWorldRotations.clear();

		instance.Initialized = false;
		instance.CreationFailed = false;
		instance.ConstraintsCreated = false;
		instance.State = RagdollState::Animated;
		instance.BlendWeight = 0.0f;
	}

	bool RagdollSystem::ComputeBodyTargets(Instance& instance) const
    {
		if (!instance.Initialized || !instance.Skeleton || !m_Scene)
			return false;

		const AnimationRuntime& runtime = m_Scene->GetAnimationRuntime();
		const SkeletonPose* pose = runtime.FindPose(instance.SkeletonEntity);
		glm::mat4 baseWorld;
		if (!pose || !runtime.TryGetSkeletonWorldTransform(instance.SkeletonEntity, baseWorld))
			return false;

		const size_t jointCount = instance.JointBodies.size();
		if (pose->Global.size() != jointCount)
			return false;

		for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
		{
			const glm::mat4 world = baseWorld * pose->Global[jointIndex];
			const glm::vec3 jointPosition = glm::vec3(world[3]);
			const glm::quat jointRotation = RotationFromMatrix(world);

			const glm::quat bodyRotation = glm::normalize(
				jointRotation * glm::inverse(instance.JointLocalRotations[jointIndex]));
			const glm::vec3 bodyPosition =
				jointPosition - bodyRotation * instance.JointLocalPositions[jointIndex];

			instance.TargetBodyPositions[jointIndex] = bodyPosition;
			instance.TargetBodyRotations[jointIndex] = bodyRotation;
		}

		return true;
	}

	void RagdollSystem::SyncKinematicBodies(Instance& instance, const float deltaTime) const
    {
		const size_t jointCount = instance.JointBodies.size();
		for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
		{
			const PhysicsBodyHandle body = instance.JointBodies[jointIndex];
			if (body == 0)
				continue;

			GetPhysics().MoveKinematicBody(
				body,
				instance.TargetBodyPositions[jointIndex],
				instance.TargetBodyRotations[jointIndex],
				deltaTime);

			instance.PreviousBodyPositions[jointIndex] = instance.TargetBodyPositions[jointIndex];
			instance.PreviousBodyRotations[jointIndex] = instance.TargetBodyRotations[jointIndex];
		}
	}

	void RagdollSystem::CreateConstraints(Instance& instance) const
    {
		if (!instance.Initialized || instance.ConstraintsCreated)
			return;

		const size_t jointCount = instance.JointBodies.size();
		instance.JointConstraints.assign(jointCount, 0);

		for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
		{
			instance.JointWorldPositions[jointIndex] =
				instance.TargetBodyPositions[jointIndex] +
				instance.TargetBodyRotations[jointIndex] * instance.JointLocalPositions[jointIndex];
			instance.JointWorldRotations[jointIndex] =
				instance.TargetBodyRotations[jointIndex] * instance.JointLocalRotations[jointIndex];
		}

		for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
		{
			const int32_t parent = instance.Skeleton->GetJoints()[jointIndex].Parent;
			if (parent < 0 || static_cast<size_t>(parent) >= jointCount)
				continue;

			const PhysicsBodyHandle bodyA = instance.JointBodies[jointIndex];
			const PhysicsBodyHandle bodyB = instance.JointBodies[static_cast<size_t>(parent)];
			if (bodyA == 0 || bodyB == 0)
				continue;

			const glm::vec3 pivot = instance.JointWorldPositions[static_cast<size_t>(parent)];
			glm::vec3 twistAxis =
				instance.JointWorldPositions[jointIndex] - instance.JointWorldPositions[static_cast<size_t>(parent)];
			if (glm::length2(twistAxis) < 1.0e-8f)
				twistAxis = glm::vec3(0.0f, 1.0f, 0.0f);
			else
				twistAxis = glm::normalize(twistAxis);

			glm::vec3 planeAxis = glm::cross(twistAxis, glm::vec3(0.0f, 1.0f, 0.0f));
			if (glm::length2(planeAxis) < 1.0e-8f)
				planeAxis = glm::cross(twistAxis, glm::vec3(1.0f, 0.0f, 0.0f));
			planeAxis = glm::normalize(planeAxis);

			float swingLimit = 0.6f;
			float twistLimit = 0.5f;
			if (instance.Skeleton)
				JointLimitsForName(instance.Skeleton->GetJoints()[jointIndex].Name, swingLimit, twistLimit);

			PhysicsConstraintDesc desc{};
			desc.BodyA = bodyA;
			desc.BodyB = bodyB;
			desc.Position = pivot;
			desc.TwistAxis = twistAxis;
			desc.PlaneAxis = planeAxis;
			desc.SwingLimit = swingLimit;
			desc.TwistMin = -twistLimit;
			desc.TwistMax = twistLimit;

			const PhysicsConstraintHandle constraint = GetPhysics().CreateConstraint(desc);
			if (constraint == 0)
			{
				Log::CoreWarn("Failed to create ragdoll constraint for joint {} on skeleton entity {}",
				              jointIndex, instance.SkeletonEntity);
				continue;
			}

			instance.JointConstraints[jointIndex] = constraint;
		}

		instance.ConstraintsCreated = true;
	}

	void RagdollSystem::DestroyConstraints(Instance& instance) const
    {
		for (const PhysicsConstraintHandle constraint : instance.JointConstraints)
		{
			if (constraint != 0)
				GetPhysics().DestroyConstraint(constraint);
		}

		instance.JointConstraints.clear();
		instance.ConstraintsCreated = false;
	}

	void RagdollSystem::BeginRagdoll(Instance& instance, const float deltaTime) const
    {
		if (!instance.Initialized)
			return;

		if (instance.State == RagdollState::Ragdoll ||
		    instance.State == RagdollState::BlendingToRagdoll)
		{
			return;
		}

		CreateConstraints(instance);

		const size_t jointCount = instance.JointBodies.size();
		for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
		{
			const PhysicsBodyHandle body = instance.JointBodies[jointIndex];
			if (body == 0)
				continue;

			glm::vec3 linearVelocity(0.0f);
			glm::vec3 angularVelocity(0.0f);

			if (deltaTime > 0.0f)
			{
				linearVelocity = (instance.TargetBodyPositions[jointIndex] -
				                  instance.PreviousBodyPositions[jointIndex]) / deltaTime;

				const glm::quat delta =
					glm::normalize(instance.TargetBodyRotations[jointIndex] *
					               glm::inverse(instance.PreviousBodyRotations[jointIndex]));
				const float angle = glm::angle(delta);
				if (angle > 1.0e-5f)
					angularVelocity = glm::axis(delta) * (angle / deltaTime);
			}

			GetPhysics().SetBodyMotionType(body, PhysicsMotionType::Dynamic);
			GetPhysics().SetLinearVelocity(body, linearVelocity);
			GetPhysics().SetAngularVelocity(body, angularVelocity);

			instance.PreviousBodyPositions[jointIndex] = instance.TargetBodyPositions[jointIndex];
			instance.PreviousBodyRotations[jointIndex] = instance.TargetBodyRotations[jointIndex];
		}

		instance.BlendWeight = 0.0f;
		instance.State = instance.BlendDuration > 0.0f
			? RagdollState::BlendingToRagdoll
			: RagdollState::Ragdoll;

		if (instance.State == RagdollState::Ragdoll)
			instance.BlendWeight = 1.0f;
	}

	void RagdollSystem::FinishRecovery(Instance& instance) const
    {
		DestroyConstraints(instance);

		for (const PhysicsBodyHandle body : instance.JointBodies)
		{
			if (body == 0)
				continue;

			GetPhysics().SetBodyMotionType(body, PhysicsMotionType::Kinematic);
			GetPhysics().SetLinearVelocity(body, glm::vec3(0.0f));
			GetPhysics().SetAngularVelocity(body, glm::vec3(0.0f));
		}

		instance.State = RagdollState::Animated;
		instance.BlendWeight = 0.0f;

		if (ComputeBodyTargets(instance))
			SyncKinematicBodies(instance, 0.0f);
	}

	void RagdollSystem::UpdateJointWorldsFromBodies(Instance& instance) const
    {
		const size_t jointCount = instance.JointBodies.size();
		for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
		{
			const PhysicsBodyHandle body = instance.JointBodies[jointIndex];
			if (body == 0)
				continue;

			glm::vec3 bodyPosition;
			glm::quat bodyRotation;
			GetPhysics().GetBodyTransform(body, bodyPosition, bodyRotation);

			instance.JointWorldPositions[jointIndex] =
				bodyPosition + bodyRotation * instance.JointLocalPositions[jointIndex];
			instance.JointWorldRotations[jointIndex] =
				bodyRotation * instance.JointLocalRotations[jointIndex];
		}
	}
}
