
#include "JoltPhysicsSystem.hpp"
#include "Core/Core.hpp"
#include "Physics/PhysicsSystem.hpp"
#include "BodyActivationListener.hpp"
#include "ContactListener.hpp"
#include "JoltImpl.hpp"
#include "Layers.hpp"
#include "Utils.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Profiling/Profilers.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <unordered_set>


import Kerberos;

namespace Kerberos
{
	namespace
	{
		JPH::EMotionType ToJoltMotionType(const PhysicsMotionType type)
		{
			switch (type)
			{
			case PhysicsMotionType::Static: return JPH::EMotionType::Static;
			case PhysicsMotionType::Kinematic: return JPH::EMotionType::Kinematic;
			case PhysicsMotionType::Dynamic: return JPH::EMotionType::Dynamic;
			}

			KBRAssert(false, "Unknown physics motion type!");
			return JPH::EMotionType::Static;
		}

		JPH::Vec3 ToJoltVec3(const glm::vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		JPH::RVec3 ToJoltRVec3(const glm::vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		JPH::Quat ToJoltQuat(const glm::quat& value)
		{
			return { value.x, value.y, value.z, value.w };
		}

		glm::vec3 ToGlmVec3(const JPH::Vec3& value)
		{
			return { value.GetX(), value.GetY(), value.GetZ() };
		}

		std::vector<OverlapHit> CollectShapeOverlaps(
			const JPH::PhysicsSystem& system,
			const JPH::Shape& shape,
			const JPH::RMat44& transform,
			const std::unordered_map<uint32_t, UUID>& bodyToEntity)
		{
			JPH::CollideShapeSettings settings;
			settings.mMaxSeparationDistance = 0.0f;

			JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
			system.GetNarrowPhaseQuery().CollideShape(
				&shape,
				JPH::Vec3::sOne(),
				transform,
				settings,
				JPH::RVec3::sZero(),
				collector);
			collector.Sort();

			std::vector<OverlapHit> results;
			std::unordered_set<UUID> seen;
			results.reserve(collector.mHits.size());

			for (const JPH::CollideShapeResult& hit : collector.mHits)
			{
				const auto entityIt = bodyToEntity.find(hit.mBodyID2.GetIndex());
				if (entityIt == bodyToEntity.end() || !entityIt->second.IsValid())
					continue;

				if (!seen.insert(entityIt->second).second)
					continue;

				OverlapHit result;
				result.EntityID = entityIt->second;
				result.Point = ToGlmVec3(hit.mContactPointOn2);
				result.Normal = hit.mPenetrationAxis.LengthSq() > 1.0e-8f
					? ToGlmVec3(-hit.mPenetrationAxis.Normalized())
					: glm::vec3(0.0f);
				results.push_back(result);
			}

			return results;
		}
	}

	struct JoltPhysicsSystem::Extras
	{
		JPH::Ref<JPH::GroupFilterTable> SelfCollisionFilter;
		std::unordered_map<PhysicsConstraintHandle, JPH::Ref<JPH::Constraint>> Constraints;
		PhysicsConstraintHandle NextConstraintHandle = 1;
	};

	JoltPhysicsSystem::~JoltPhysicsSystem()
	{
		JoltPhysicsSystem::Cleanup();
	}

	void JoltPhysicsSystem::Initialize(const Ref<Scene>& scene)
	{
		KBR_TRACY_FUNCTION();

		m_Scene = scene;

		/// Register allocation hook. In this example we'll just let Jolt use malloc / free but you can override these if you want (see Memory.h).
		/// This needs to be done before any other Jolt function is called.
		JPH::RegisterDefaultAllocator();

		/// Install trace and assert callbacks
		JPH::Trace = Physics::TraceImpl;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = Physics::AssertFailedImpl;);

		/// Create a factory, this class is responsible for creating instances of classes based on their name or hash and is mainly used for deserialization of saved data.
		/// It is not directly used in this example but still required.
		JPH::Factory::sInstance = new JPH::Factory();

		/// Register all physics types with the factory and install their collision handlers with the CollisionDispatch class.
		/// If you have your own custom shape types you probably need to register their handlers with the CollisionDispatch before calling this function.
		/// If you implement your own default material (PhysicsMaterial::sDefault) make sure to initialize it before this function or 
		/// else this function will create one for you.
		JPH::RegisterTypes();

		delete m_Extras;
		m_Extras = new Extras();

		// Shared self-collision filter for ragdoll bodies. 
		// Every intra-group sub-group pair is disabled. 
		// Different collision groups (separate ragdolls) still collide normally.
		constexpr uint32_t cMaxRagdollSubGroups = 256;
		m_Extras->SelfCollisionFilter = new JPH::GroupFilterTable(cMaxRagdollSubGroups);
		for (uint32_t groupA = 0; groupA < cMaxRagdollSubGroups; ++groupA)
		{
			for (uint32_t groupB = groupA + 1; groupB < cMaxRagdollSubGroups; ++groupB)
				m_Extras->SelfCollisionFilter->DisableCollision(groupA, groupB);
		}

		/// We need a temp allocator for temporary allocations during the physics update. We're
		/// pre-allocating 10 MB to avoid having to do allocations during the physics update.
		/// If you don't want to pre-allocate you can also use TempAllocatorMalloc to fall back to
		/// malloc / free.
		constexpr size_t cTempAllocatorSize = 10ull * 1024ull * 1024ull; /// 10 MB
		m_PhysicsTempAllocator = new JPH::TempAllocatorImpl(cTempAllocatorSize);

		/// We need a job system that will execute physics jobs on multiple threads. Typically
		/// you would implement the JobSystem interface yourself and let Jolt PhysicsTemp run on top
		/// of your own job scheduler. JobSystemThreadPool is an example implementation.
		m_PhysicsJobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(std::thread::hardware_concurrency()) - 1);

		/// This is the max amount of rigid bodies that you can add to the physics system. Ragdolls
		/// create one body per joint, so keep enough headroom for several characters.
		constexpr uint32_t cMaxBodies = 4096;

		/// This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
		constexpr uint32_t cNumBodyMutexes = 0;

		/// This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
		/// body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
		/// too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
		constexpr uint32_t cMaxBodyPairs = 65536;

		/// This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
		/// number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
		constexpr uint32_t cMaxContactConstraints = 10240;

		/// Create mapping table from object layer to broadphase layer
		/// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
		/// Also have a look at BroadPhaseLayerInterfaceTable or BroadPhaseLayerInterfaceMask for a simpler interface.
		m_BroadPhaseLayerInterface = new Physics::BroadPhaseLayerInterfaceImpl();

		/// Create class that filters object vs broadphase layers
		/// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
		/// Also have a look at ObjectVsBroadPhaseLayerFilterTable or ObjectVsBroadPhaseLayerFilterMask for a simpler interface.
		m_ObjectVsBroadPhaseLayerFilter = new Physics::ObjectVsBroadPhaseLayerFilterImpl();

		/// Create class that filters object vs object layers
		/// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
		/// Also have a look at ObjectLayerPairFilterTable or ObjectLayerPairFilterMask for a simpler interface.
		m_ObjectVsObjectLayerFilter = new Physics::ObjectLayerPairFilterImpl();

		m_JoltSystem = new JPH::PhysicsSystem();
		m_JoltSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, *m_BroadPhaseLayerInterface, *m_ObjectVsBroadPhaseLayerFilter, *m_ObjectVsObjectLayerFilter);

		/// A body activation listener gets notified when bodies activate and go to sleep
		/// Note that this is called from a job so whatever you do here needs to be thread safe.
		/// Registering one is entirely optional.
		m_BodyActivationListener = new Physics::BodyActivationListener();
		m_JoltSystem->SetBodyActivationListener(m_BodyActivationListener);

		/// A contact listener gets notified when bodies (are about to) collide, and when they separate again.
		/// Note that this is called from a job so whatever you do here needs to be thread safe.
		/// Registering one is entirely optional.
		m_ContactListener = new Physics::ContactListener();
		m_JoltSystem->SetContactListener(m_ContactListener);
	}


	void JoltPhysicsSystem::Update(const float deltaTime)
	{
		KBR_TRACY_FUNCTION();

		UpdateAndCreatePhysicsBodies();

		SyncKinematicBodies(deltaTime);

		constexpr int collisionSteps = 2;
		m_JoltSystem->Update(deltaTime, collisionSteps, m_PhysicsTempAllocator, m_PhysicsJobSystem);

		SyncTransforms();
	}

	void JoltPhysicsSystem::AddImpulse(const uint32_t bodyId, const glm::vec3& impulse) const
	{
		const JPH::BodyID id{ bodyId };
		JPH::BodyInterface& bodyInterface = GetBodyInterface();
		const JPH::Vec3Arg joltForce(impulse.x, impulse.y, impulse.z);
		bodyInterface.AddImpulse(id, joltForce);
	}

	void JoltPhysicsSystem::AddImpulse(const uint32_t bodyId, const glm::vec3& impulse, const glm::vec3& point) const
	{
		const JPH::BodyID id{ bodyId };
		JPH::BodyInterface& bodyInterface = GetBodyInterface();
		const JPH::Vec3Arg joltForce(impulse.x, impulse.y, impulse.z);
		const JPH::Vec3Arg joltPoint(point.x, point.y, point.z);
		bodyInterface.AddImpulse(id, joltForce, joltPoint);
	}

	void JoltPhysicsSystem::SetLinearVelocity(const uint32_t bodyId, const glm::vec3& velocity) const
	{
		const JPH::BodyID id{ bodyId };
		JPH::BodyInterface& bodyInterface = GetBodyInterface();
		const JPH::Vec3Arg joltVelocity(velocity.x, velocity.y, velocity.z);
		bodyInterface.SetLinearVelocity(id, joltVelocity);
	}

	void JoltPhysicsSystem::SetAngularVelocity(const uint32_t bodyId, const glm::vec3& velocity) const
	{
		const JPH::BodyID id{ bodyId };
		JPH::BodyInterface& bodyInterface = GetBodyInterface();
		const JPH::Vec3Arg joltVelocity(velocity.x, velocity.y, velocity.z);
		bodyInterface.SetAngularVelocity(id, joltVelocity);
	}

	glm::vec3 JoltPhysicsSystem::GetLinearVelocity(const uint32_t bodyId) const
	{
		const JPH::Vec3 velocity = GetBodyInterface().GetLinearVelocity(JPH::BodyID{ bodyId });
		return ToGlmVec3(velocity);
	}

	glm::vec3 JoltPhysicsSystem::GetAngularVelocity(const uint32_t bodyId) const
	{
		const JPH::Vec3 velocity = GetBodyInterface().GetAngularVelocity(JPH::BodyID{ bodyId });
		return ToGlmVec3(velocity);
	}

	PhysicsBodyHandle JoltPhysicsSystem::CreateBody(const PhysicsBodyDesc& desc)
	{
		KBR_TRACY_FUNCTION();

		if (!m_JoltSystem || !std::isfinite(desc.Mass) || desc.Mass <= 0.0f)
			return 0;

		JPH::RefConst<JPH::Shape> shape;
		switch (desc.Shape.Type)
		{
		case PhysicsShapeType::Box:
			shape = new JPH::BoxShape(ToJoltVec3(desc.Shape.HalfExtents));
			break;
		case PhysicsShapeType::Sphere:
			shape = new JPH::SphereShape(desc.Shape.Radius);
			break;
		case PhysicsShapeType::Capsule:
			shape = new JPH::CapsuleShape(desc.Shape.HalfHeight, desc.Shape.Radius);
			break;
		}

		if (!shape)
			return 0;

		JPH::BodyCreationSettings settings(
			shape.GetPtr(),
			ToJoltRVec3(desc.Position),
			ToJoltQuat(desc.Rotation),
			ToJoltMotionType(desc.MotionType),
			Physics::Layers::MOVING);

		settings.mFriction = desc.Friction;
		settings.mRestitution = desc.Restitution;
		settings.mLinearDamping = desc.LinearDamping;
		settings.mAngularDamping = desc.AngularDamping;
		settings.mGravityFactor = desc.UseGravity ? 1.0f : 0.0f;
		settings.mAllowSleeping = desc.AllowSleeping;
		settings.mIsSensor = false;

		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		settings.mMassPropertiesOverride.mMass = desc.Mass;

		if (desc.CollisionGroup != 0xFFFFFFFFu && m_Extras && m_Extras->SelfCollisionFilter)
		{
			settings.mCollisionGroup = JPH::CollisionGroup(
				m_Extras->SelfCollisionFilter.GetPtr(),
				desc.CollisionGroup,
				desc.CollisionSubGroup);
		}

		JPH::BodyInterface& bodyInterface = GetBodyInterface();
		JPH::Body* body = bodyInterface.CreateBody(settings);
		if (!body)
			return 0;

		const JPH::BodyID bodyId = body->GetID();
		bodyInterface.AddBody(bodyId, JPH::EActivation::Activate);

		if (desc.UserData.IsValid())
		{
			m_BodyIDToEntityID[bodyId.GetIndex()] = desc.UserData;
			body->SetUserData(desc.UserData);
		}

		return bodyId.GetIndexAndSequenceNumber();
	}

	void JoltPhysicsSystem::DestroyBody(const PhysicsBodyHandle body)
	{
		if (!m_JoltSystem || body == 0)
			return;

		const JPH::BodyID id{ body };
		m_BodyIDToEntityID.erase(id.GetIndex());

		JPH::BodyInterface& bodyInterface = GetBodyInterface();
		bodyInterface.RemoveBody(id);
		bodyInterface.DestroyBody(id);
	}

	void JoltPhysicsSystem::SetBodyTransform(const PhysicsBodyHandle body,
	                                         const glm::vec3& position,
	                                         const glm::quat& rotation)
	{
		if (!m_JoltSystem || body == 0)
			return;

		GetBodyInterface().SetPositionAndRotation(
			JPH::BodyID{ body },
			ToJoltRVec3(position),
			ToJoltQuat(rotation),
			JPH::EActivation::Activate);
	}

	void JoltPhysicsSystem::MoveKinematicBody(const PhysicsBodyHandle body,
	                                          const glm::vec3& position,
	                                          const glm::quat& rotation,
	                                          const float deltaTime)
	{
		if (!m_JoltSystem || body == 0)
			return;

		JPH::BodyInterface& bodyInterface = GetBodyInterface();
		const JPH::BodyID id{ body };

		if (deltaTime > 0.0f)
		{
			bodyInterface.MoveKinematic(id, ToJoltRVec3(position), ToJoltQuat(rotation), deltaTime);
			bodyInterface.ActivateBody(id);
		}
		else
		{
			bodyInterface.SetPositionAndRotation(id, ToJoltRVec3(position), ToJoltQuat(rotation),
			                                     JPH::EActivation::Activate);
		}
	}

	void JoltPhysicsSystem::GetBodyTransform(const PhysicsBodyHandle body,
	                                         glm::vec3& outPosition,
	                                         glm::quat& outRotation) const
	{
		if (!m_JoltSystem || body == 0)
		{
			outPosition = glm::vec3(0.0f);
			outRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			return;
		}

		JPH::RVec3 position;
		JPH::Quat rotation;
		GetBodyInterface().GetPositionAndRotation(JPH::BodyID{ body }, position, rotation);

		outPosition = Physics::Utils::ToGlmVec3(position);
		outRotation = Physics::Utils::ToGlmQuat(rotation);
	}

	void JoltPhysicsSystem::SetBodyMotionType(const PhysicsBodyHandle body, const PhysicsMotionType type)
	{
		if (!m_JoltSystem || body == 0)
			return;

		GetBodyInterface().SetMotionType(
			JPH::BodyID{ body },
			ToJoltMotionType(type),
			JPH::EActivation::Activate);
	}

	PhysicsConstraintHandle JoltPhysicsSystem::CreateConstraint(const PhysicsConstraintDesc& desc)
	{
		KBR_TRACY_FUNCTION();

		if (!m_JoltSystem || desc.BodyA == 0 || desc.BodyB == 0)
			return 0;

		JPH::Body* bodyA = m_JoltSystem->GetBodyLockInterface().TryGetBody(JPH::BodyID{ desc.BodyA });
		JPH::Body* bodyB = m_JoltSystem->GetBodyLockInterface().TryGetBody(JPH::BodyID{ desc.BodyB });
		if (!bodyA || !bodyB)
		{
			Log::CoreWarn("Jolt: cannot create constraint without valid bodies");
			return 0;
		}

		JPH::Vec3 twistAxis = ToJoltVec3(desc.TwistAxis);
		if (twistAxis.LengthSq() < 1.0e-8f)
			twistAxis = JPH::Vec3::sAxisY();
		else
			twistAxis = twistAxis.Normalized();

		JPH::Vec3 planeAxis = ToJoltVec3(desc.PlaneAxis);
		planeAxis -= twistAxis * planeAxis.Dot(twistAxis);
		if (planeAxis.LengthSq() < 1.0e-8f)
		{
			const JPH::Vec3 fallback = std::abs(twistAxis.GetY()) < 0.9f
				? JPH::Vec3::sAxisY()
				: JPH::Vec3::sAxisX();
			planeAxis = fallback - twistAxis * fallback.Dot(twistAxis);
		}
		planeAxis = planeAxis.Normalized();

		JPH::SwingTwistConstraintSettings settings;
		settings.mSpace = JPH::EConstraintSpace::WorldSpace;
		settings.mPosition1 = ToJoltRVec3(desc.Position);
		settings.mPosition2 = ToJoltRVec3(desc.Position);
		settings.mTwistAxis1 = twistAxis;
		settings.mTwistAxis2 = twistAxis;
		settings.mPlaneAxis1 = planeAxis;
		settings.mPlaneAxis2 = planeAxis;
		settings.mNormalHalfConeAngle = desc.SwingLimit;
		settings.mPlaneHalfConeAngle = desc.SwingLimit;
		settings.mTwistMinAngle = desc.TwistMin;
		settings.mTwistMaxAngle = desc.TwistMax;

		JPH::TwoBodyConstraint* constraint = settings.Create(*bodyA, *bodyB);
		if (!constraint)
			return 0;

		m_JoltSystem->AddConstraint(constraint);

		const PhysicsConstraintHandle handle = m_Extras->NextConstraintHandle++;
		m_Extras->Constraints.emplace(handle, JPH::Ref<JPH::Constraint>(constraint));
		return handle;
	}

	void JoltPhysicsSystem::DestroyConstraint(const PhysicsConstraintHandle constraint)
	{
		if (!m_Extras)
			return;

		const auto it = m_Extras->Constraints.find(constraint);
		if (it == m_Extras->Constraints.end())
			return;

		if (m_JoltSystem)
			m_JoltSystem->RemoveConstraint(it->second.GetPtr());

		m_Extras->Constraints.erase(it);
	}

	bool JoltPhysicsSystem::Raycast(const glm::vec3& origin, const glm::vec3& direction, const float maxDistance,
		RaycastHit& outHit) const 
	{
		KBR_TRACY_FUNCTION();

		if (maxDistance <= 0.0f)
		{
			return false;
		}

		const float directionLength = glm::length(direction);
		if (directionLength <= 1e-6f)
		{
			return false;
		}

		// Physics raycasts expect world-space direction vectors.
		const glm::vec3 normalizedDirection = direction / directionLength;

		const JPH::RRayCast ray{
			JPH::Vec3(origin.x, origin.y, origin.z),
			JPH::Vec3(normalizedDirection.x, normalizedDirection.y, normalizedDirection.z) * maxDistance
		};

		if (JPH::RayCastResult hit; m_JoltSystem->GetNarrowPhaseQuery().CastRay(ray, hit))
		{
			const JPH::Vec3 joltPoint = ray.GetPointOnRay(hit.mFraction);
			const glm::vec3 point = Physics::Utils::ToGlmVec3(joltPoint);

			const float dist = hit.mFraction * maxDistance;

			const uint32_t bodyId = hit.mBodyID.GetIndex();
			const auto entityIt = m_BodyIDToEntityID.find(bodyId);
			if (entityIt == m_BodyIDToEntityID.end() || !entityIt->second.IsValid())
				return false;

			const UUID entityId = entityIt->second;

			const JPH::BodyInterface& bodyInterface = GetBodyInterface();
			const JPH::Vec3 joltNormal = bodyInterface.GetShape(hit.mBodyID)->GetSurfaceNormal(hit.mSubShapeID2, joltPoint);
			const glm::vec3 normal = Physics::Utils::ToGlmVec3(joltNormal);

			outHit.Point = point;
			outHit.Normal = normal;
			outHit.Distance = dist;
			outHit.EntityID = entityId;

			return true;
		}

		return false;
	}

	std::vector<OverlapHit> JoltPhysicsSystem::OverlapBox(const glm::vec3& center,
	                                                      const glm::quat& rotation,
	                                                      const glm::vec3& halfExtents) const
	{
		KBR_TRACY_FUNCTION();

		if (!m_JoltSystem)
			return {};

		const glm::vec3 safeHalfExtents = glm::max(halfExtents, glm::vec3(1.0e-4f));
		const JPH::BoxShape shape(ToJoltVec3(safeHalfExtents));
		const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(
			ToJoltQuat(rotation),
			ToJoltRVec3(center));

		return CollectShapeOverlaps(*m_JoltSystem, shape, transform, m_BodyIDToEntityID);
	}

	std::vector<OverlapHit> JoltPhysicsSystem::OverlapSphere(const glm::vec3& center, const float radius) const
	{
		KBR_TRACY_FUNCTION();

		if (!m_JoltSystem || !std::isfinite(radius) || radius <= 0.0f)
			return {};

		const JPH::SphereShape shape(radius);
		const JPH::RMat44 transform = JPH::RMat44::sTranslation(ToJoltRVec3(center));

		return CollectShapeOverlaps(*m_JoltSystem, shape, transform, m_BodyIDToEntityID);
	}

    void JoltPhysicsSystem::SyncKinematicBodies(const float dt) const
    {
        const auto view = m_Scene.lock()->m_Registry.view<RigidBody3DComponent, TransformComponent>();
		for (const auto e : view)
		{
			const auto& transform = view.get<TransformComponent>(e);
			const auto& rigidBody = view.get<RigidBody3DComponent>(e);

			if (rigidBody.Type != RigidBody3DComponent::BodyType::Kinematic || !rigidBody.RuntimeBody)
				continue;

			const glm::mat4& world = transform.WorldTransform;
            const glm::vec3 position = world[3];
            const glm::quat rotation = glm::normalize(glm::quat_cast(glm::mat3(world)));

			const JPH::BodyID bodyId = static_cast<JPH::Body*>(rigidBody.RuntimeBody)->GetID();
            const JPH::Vec3 joltPosition = JPH::Vec3(position.x, position.y, position.z);
            const JPH::Quat joltRotation = JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w);

            JPH::BodyInterface& bodyInterface = GetBodyInterface();
            if (dt > 0.0f)
            {
                bodyInterface.MoveKinematic(bodyId, joltPosition, joltRotation, dt);
                bodyInterface.ActivateBody(bodyId);
            }
            else
            {
                // Initial sync / teleport: no velocity integration is possible with dt == 0.
                bodyInterface.SetPositionAndRotation(bodyId, joltPosition, joltRotation, JPH::EActivation::Activate);
            }
		}
    }

    std::vector<CollisionEvent> JoltPhysicsSystem::GetCollisionEvents() const 
	{
		return static_cast<Physics::ContactListener*>(m_ContactListener)->GetCollisionEventsAndResetQueue();
	}

	JPH::BodyInterface& JoltPhysicsSystem::GetBodyInterface() const
	{
		return m_JoltSystem->GetBodyInterface();
	}

	void JoltPhysicsSystem::UpdateAndCreatePhysicsBodies()
	{
		KBR_TRACY_FUNCTION();

		KBRAssert(!m_Scene.expired(), "Scene is not initialized!");
		KBRAssert(m_JoltSystem != nullptr, "Jolt Physics System is not initialized!");

		// Handle entities with rigidbodies but no physics body created yet
		const auto newBodies = m_Scene.lock()->m_Registry.view<RigidBody3DComponent, TransformComponent>(/*entt::exclude<>*/);
		for (const entt::entity id : newBodies)
		{
			const Entity entity(id, m_Scene);
			auto& rb = entity.GetComponent<RigidBody3DComponent>();
			/*if (rb.bodyID == JPH::BodyID::cInvalidBodyID)
			{
				CreatePhysicsBody(id);
			}*/
			if (!rb.RuntimeBody)
			{
				CreatePhysicsBody(entity);
				rb.IsDirty = false;
			}
			/*else
			{
				rb.IsDirty = true;
			}*/
		}

		/// Update existing bodies that are marked dirty
		const auto existingBodies = m_Scene.lock()->m_Registry.view<RigidBody3DComponent, TransformComponent>();
		for (const entt::entity id : existingBodies)
		{
			const Entity entity(id, m_Scene);
			auto& rb = entity.GetComponent<RigidBody3DComponent>();
			if (rb.IsDirty)
			{
				/// Remove the old body if it exists
				if (rb.RuntimeBody)
				{
					/// TODO: Do not remove and destroy the old body, but modify it
					JPH::BodyInterface& bodyInterface = m_JoltSystem->GetBodyInterface();
					bodyInterface.RemoveBody(static_cast<JPH::Body*>(rb.RuntimeBody)->GetID());
					bodyInterface.DestroyBody(static_cast<JPH::Body*>(rb.RuntimeBody)->GetID());
					rb.RuntimeBody = nullptr;
				}

				CreatePhysicsBody(entity);
				rb.IsDirty = false;
			}
		}
	}

	void JoltPhysicsSystem::SyncTransforms() const
	{
		KBR_TRACY_FUNCTION();

		KBRAssert(!m_Scene.expired(), "Scene is not initialized!");
		KBRAssert(m_JoltSystem != nullptr, "Jolt Physics System is not initialized!");

		const auto view = m_Scene.lock()->m_Registry.view<RigidBody3DComponent, TransformComponent>();
		for (const auto e : view)
		{
			auto& transform = view.get<TransformComponent>(e);
			auto& rigidBody = view.get<RigidBody3DComponent>(e);

			// Only dynamic bodies are read back from the physics engine. Static and kinematic
			// bodies are controlled by the user/scene.
			if (rigidBody.Type == RigidBody3DComponent::BodyType::Static || rigidBody.Type == RigidBody3DComponent::BodyType::Kinematic) 
				continue;

			if (rigidBody.RuntimeBody)
			{
				const JPH::Body* body = static_cast<JPH::Body*>(rigidBody.RuntimeBody);

				const Entity entity(e, m_Scene);
				Physics::Utils::ApplyJoltTransformToEntity(transform.WorldTransform, *body, entity.GetComponent<TransformComponent>());

				// Sync velocity for access by other systems
				JPH::Vec3 vel = body->GetLinearVelocity();
				JPH::Vec3 angVel =	body->GetAngularVelocity();
				rigidBody.Velocity = glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());
				rigidBody.AngularVelocity = glm::vec3(angVel.GetX(), angVel.GetY(), angVel.GetZ());
			}
		}

		/// There probably is a better solution, but lets just stick to something that mostly works for now.

		//const JPH::BodyInterface& bodyInterface = m_JoltSystem->GetBodyInterface();

		//const auto view = m_Scene->m_Registry.view<RigidBody3DComponent, TransformComponent>();
		//for (const entt::entity id : view)
		//{
		//	const Entity entity(id, m_Scene);
		//	auto& rb = entity.GetComponent<RigidBody3DComponent>();
		//	auto& transform = entity.GetComponent<TransformComponent>();

		//	if (rb.bodyID == JPH::BodyID::cInvalidBodyID) continue;
		//	if (rb.motionType == JPH::EMotionType::Static) continue;

		//	// Get position and rotation from physics body
		//	JPH::RVec3 position = bodyInterface.GetPosition(rb.bodyID);
		//	JPH::Quat rotation = bodyInterface.GetRotation(rb.bodyID);

		//	// Update transform
		//	transform.Translation = glm::vec3(position.GetX(), position.GetY(), position.GetZ());
		//	transform.Rotation = glm::quat(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());

		//	// Update velocity for access by other systems
		//	JPH::Vec3 vel = bodyInterface.GetLinearVelocity(rb.bodyID);
		//	JPH::Vec3 angVel = bodyInterface.GetAngularVelocity(rb.bodyID);
		//	rb.Velocity = glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());
		//	rb.AngularVelocity = glm::vec3(angVel.GetX(), angVel.GetY(), angVel.GetZ());
		//}
	}

	void JoltPhysicsSystem::CreatePhysicsBody(const Entity& entity)
	{
		KBR_TRACY_FUNCTION();

		auto& rigidBody = entity.GetComponent<RigidBody3DComponent>();
		auto& transform = entity.GetComponent<TransformComponent>();

		const JPH::RefConst<JPH::Shape> shape = CreateShapeForEntity(entity);
		const auto shapeRef = shape.GetPtr();
		if (!shape) return; // No colliders found

		const glm::vec4 worldPos = transform.WorldTransform[3];
		const float posX = worldPos.x; //+ collider.Offset.x;
		const float posY = worldPos.y; //+ collider.Offset.y;
		const float posZ = worldPos.z; //+ collider.Offset.z;

		const JPH::Vec3 position = JPH::Vec3(posX, posY, posZ);

		/// Extract rotation quaternion from the transform matrix
		glm::quat glmRotation = glm::quat_cast(glm::mat3(transform.WorldTransform));
		glmRotation = glm::normalize(glmRotation); /// Ensure the quaternion is normalized
		const JPH::Quat rotation = JPH::Quat(glmRotation.x, glmRotation.y, glmRotation.z, glmRotation.w);

		JPH::BodyCreationSettings bodySettings(
			shapeRef,
			position,
			rotation,
			Physics::Utils::GetJPHMotionTypeFromComponent(rigidBody), Physics::Utils::GetObjectLayerFromComponent(rigidBody)
		);

		const bool isTrigger = IsColliderTrigger(entity);

		bodySettings.mIsSensor = isTrigger;
		bodySettings.mMassPropertiesOverride.mMass = rigidBody.Mass;
		bodySettings.mFriction = rigidBody.Friction;
		bodySettings.mRestitution = rigidBody.Restitution;
		//bodySettings.mLinearDamping = rigidBody.LinearDamping;
		//bodySettings.mAngularDamping = rigidBody.AngularDamping;
		bodySettings.mGravityFactor = rigidBody.UseGravity ? 1.0f : 0.0f;

		bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		auto massProperties = JPH::MassProperties();
		massProperties.mMass = rigidBody.Mass;
		bodySettings.mMassPropertiesOverride = massProperties;

		JPH::BodyInterface& bodyInterface = m_JoltSystem->GetBodyInterface();
		JPH::Body* body = bodyInterface.CreateBody(bodySettings);
		rigidBody.RuntimeBody = body;

		const auto& bodyId = body->GetID();
		m_BodyIDToEntityID[bodyId.GetIndex()] = entity.GetUUID();

		body->SetUserData(entity.GetUUID());

		// Set starting velocity
		bodyInterface.SetLinearVelocity(bodyId, JPH::Vec3(rigidBody.Velocity.x, rigidBody.Velocity.y, rigidBody.Velocity.z));
		bodyInterface.SetAngularVelocity(bodyId, JPH::Vec3(rigidBody.AngularVelocity.x, rigidBody.AngularVelocity.y, rigidBody.AngularVelocity.z));

		bodyInterface.AddBody(bodyId, JPH::EActivation::Activate);

		rigidBody.IsDirty = false;
	}

	JPH::RefConst<JPH::Shape> JoltPhysicsSystem::CreateShapeForEntity(const Entity& entity)
	{
		struct PositionedShape
		{
			JPH::Vec3 Offset;
			JPH::RefConst<JPH::Shape> Shape;
		};

		std::vector<PositionedShape> shapes;

		if (entity.HasComponent<BoxCollider3DComponent>())
		{
			const auto& coll = entity.GetComponent<BoxCollider3DComponent>();
			if (coll.Enabled)
			{
				shapes.push_back({
					.Offset = JPH::Vec3(coll.Offset.x, coll.Offset.y, coll.Offset.z),
					.Shape = new JPH::BoxShape(JPH::Vec3(coll.Size.x, coll.Size.y, coll.Size.z))
				});
			}
		}

		if (entity.HasComponent<SphereCollider3DComponent>())
		{
			const auto& coll = entity.GetComponent<SphereCollider3DComponent>();
			if (coll.Enabled)
			{
				shapes.push_back({
					.Offset = JPH::Vec3(coll.Offset.x, coll.Offset.y, coll.Offset.z),
					.Shape = new JPH::SphereShape(coll.Radius)
				});
			}
		}

		if (entity.HasComponent<CapsuleCollider3DComponent>())
		{
			const auto& coll = entity.GetComponent<CapsuleCollider3DComponent>();
			if (coll.Enabled)
			{
				shapes.push_back({
					.Offset = JPH::Vec3(coll.Offset.x, coll.Offset.y, coll.Offset.z),
					.Shape = new JPH::CapsuleShape(coll.Height / 2, coll.Radius)
				});
			}
		}

		if (entity.HasComponent<MeshCollider3DComponent>())
		{
			const auto& coll = entity.GetComponent<MeshCollider3DComponent>();
			const Ref<Mesh> mesh = coll.Mesh
				? coll.Mesh
				: (entity.HasComponent<StaticMeshComponent>()
					? entity.GetComponent<StaticMeshComponent>().StaticMesh
					: nullptr);
			if (coll.Enabled && mesh)
			{
				if (const JPH::RefConst<JPH::Shape> shape = Physics::Utils::CreateJoltMeshShape(mesh, entity.GetName()))
				{
					shapes.push_back({
						.Offset = JPH::Vec3(coll.Offset.x, coll.Offset.y, coll.Offset.z),
						.Shape = shape
					});
				}
			}
		}

		if (shapes.empty()) return nullptr;

		if (shapes.size() == 1)
		{
			return new JPH::RotatedTranslatedShape(shapes[0].Offset, JPH::Quat::sIdentity(), shapes[0].Shape.GetPtr());
		}

		JPH::StaticCompoundShapeSettings compoundSettings;
		for (const auto& [Offset, Shape] : shapes)
		{
			compoundSettings.AddShape(Offset, JPH::Quat::sIdentity(), Shape);
		}

		const JPH::ShapeSettings::ShapeResult compoundResult = compoundSettings.Create();
		if (compoundResult.HasError())
		{
			Kerberos::Log::CoreError("Jolt: failed to create compound shape for entity {}: {}", entity.GetName(), compoundResult.GetError().c_str());
			return nullptr;
		}
		return compoundResult.Get();
	}

	bool JoltPhysicsSystem::IsColliderTrigger(const Entity& entity)
	{
		bool isTrigger = false;

		if (entity.HasComponent<BoxCollider3DComponent>())
		{
			const auto& collider = entity.GetComponent<BoxCollider3DComponent>();
			isTrigger = isTrigger || (collider.Enabled && collider.IsTrigger);
		}
		if (entity.HasComponent<SphereCollider3DComponent>())
		{
			const auto& collider = entity.GetComponent<SphereCollider3DComponent>();
			isTrigger = isTrigger || (collider.Enabled && collider.IsTrigger);
		}
		if (entity.HasComponent<CapsuleCollider3DComponent>())
		{
			const auto& collider = entity.GetComponent<CapsuleCollider3DComponent>();
			isTrigger = isTrigger || (collider.Enabled && collider.IsTrigger);
		}
		if (entity.HasComponent<MeshCollider3DComponent>())
		{
			const auto& collider = entity.GetComponent<MeshCollider3DComponent>();
			isTrigger = isTrigger || (collider.Enabled && collider.IsTrigger);
		}

		return isTrigger;
	}

	void JoltPhysicsSystem::Cleanup()
	{
		KBR_TRACY_FUNCTION();

		//const auto view = m_Scene->m_Registry.view<RigidBody3DComponent>();
		//for (const auto e : view)
		//{
		//	auto& rigidbody = view.get<RigidBody3DComponent>(e);
		//	rigidbody.RuntimeBody = nullptr;
		//}

		m_BodyIDToEntityID.clear();

		// Constraints must be detached from the physics system before it is destroyed.
		if (m_JoltSystem && m_Extras)
		{
			for (const auto& constraint : m_Extras->Constraints | std::views::values)
				m_JoltSystem->RemoveConstraint(constraint.GetPtr());
		}
		if (m_Extras)
			m_Extras->Constraints.clear();

		delete m_ContactListener;
		m_ContactListener = nullptr;

		delete m_BodyActivationListener;
		m_BodyActivationListener = nullptr;

		delete m_JoltSystem;
		m_JoltSystem = nullptr;

		delete m_Extras;
		m_Extras = nullptr;

		delete m_ObjectVsObjectLayerFilter;
		m_ObjectVsObjectLayerFilter = nullptr;

		delete m_ObjectVsBroadPhaseLayerFilter;
		m_ObjectVsBroadPhaseLayerFilter = nullptr;

		delete m_BroadPhaseLayerInterface;
		m_BroadPhaseLayerInterface = nullptr;

		delete m_PhysicsJobSystem;
		m_PhysicsJobSystem = nullptr;

		delete m_PhysicsTempAllocator;
		m_PhysicsTempAllocator = nullptr;

		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;

		JPH::Trace = nullptr;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = nullptr;);
	}
}