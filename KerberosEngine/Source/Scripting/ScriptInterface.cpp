
#include "ScriptInterface.hpp"

#include "Input/InputSystem.hpp"
#include "Input/KeyCodes.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Components/AudioComponents.hpp"
#include "Assets/AssetManager.hpp"
#include "Profiling/Profilers.hpp"

#include <algorithm>

#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>

#include <memory>


import Kerberos;

namespace Kerberos
{
	/// Structure matching the layout expected by C# SetNativeCallbacks.
	/// Each entry is a function pointer to a native callback.
	struct NativeCallbackTable
	{
		void* NativeLog;
		void* Entity_HasComponent;
		void* Entity_AddComponent;
		void* Entity_FindEntityByName;
		void* Entity_Instantiate;

		void* TransformComponent_GetTranslation;
		void* TransformComponent_SetTranslation;
		void* TransformComponent_GetRotation;
		void* TransformComponent_SetRotation;
		void* TransformComponent_GetScale;
		void* TransformComponent_SetScale;

		void* StaticMeshComponent_GetMesh;
		void* StaticMeshComponent_SetMesh;

		void* Rigidbody3DComponent_GetVelocity;
		void* Rigidbody3DComponent_SetVelocity;
		void* Rigidbody3DComponent_ApplyImpulse;
		void* Rigidbody3DComponent_ApplyImpulseAtPoint;

		void* TextComponent_SetText;
		void* TextComponent_GetText;
		void* TextComponent_SetColor;
		void* TextComponent_GetColor;
		void* TextComponent_SetFontSize;
		void* TextComponent_GetFontSize;
		void* TextComponent_SetFontPath;
		void* TextComponent_GetFontPath;

		void* Input_IsKeyDown;
		void* Input_IsMouseButtonDown;

		void* AudioSource2DComponent_Play;
		void* AudioSource2DComponent_Stop;
		void* AudioSource2DComponent_SetVolume;
		void* AudioSource2DComponent_GetVolume;
		void* AudioSource2DComponent_SetLooping;
		void* AudioSource2DComponent_IsLooping;

		void* AudioSource3DComponent_Play;
		void* AudioSource3DComponent_Stop;
		void* AudioSource3DComponent_SetVolume;
		void* AudioSource3DComponent_GetVolume;
		void* AudioSource3DComponent_SetLooping;
		void* AudioSource3DComponent_IsLooping;

		void* Physics_Raycast;
		void* Physics_OverlapBox;
		void* Physics_OverlapBoxForEntity;
		void* Physics_OverlapSphere;
		void* Physics_OverlapSphereForEntity;

		void* Entity_InstantiatePrefab;

		void* AnimatorComponent_Play;
		void* AnimatorComponent_PlayByName;
		void* AnimatorComponent_Stop;
		void* AnimatorComponent_Pause;
		void* AnimatorComponent_GetTime;
		void* AnimatorComponent_SetTime;
		void* AnimatorComponent_GetSpeed;
		void* AnimatorComponent_SetSpeed;
		void* AnimatorComponent_IsPlaying;
		void* AnimatorComponent_SetPlaying;
		void* AnimatorComponent_IsLooping;
		void* AnimatorComponent_SetLooping;
		void* AnimatorComponent_GetPlayOnStart;
		void* AnimatorComponent_SetPlayOnStart;
		void* AnimatorComponent_GetActiveClip;
		void* AnimatorComponent_SetActiveClip;
		void* AnimatorComponent_GetClipCount;
		void* AnimatorComponent_GetClipDuration;
		void* AnimatorComponent_CrossFade;
		void* AnimatorComponent_CrossFadeByName;

		void* Socket_Attach;
		void* Socket_Detach;

		void* RagdollComponent_EnableRagdoll;
		void* RagdollComponent_DisableRagdoll;
		void* RagdollComponent_IsRagdoll;
		void* RagdollComponent_GetState;
		void* RagdollComponent_GetWeight;
		void* RagdollComponent_GetBlendDuration;
		void* RagdollComponent_SetBlendDuration;
		void* RagdollComponent_GetStartInRagdoll;
		void* RagdollComponent_SetStartInRagdoll;
		void* RagdollComponent_GetJointRadiusScale;
		void* RagdollComponent_SetJointRadiusScale;

		void* Rigidbody3DComponent_GetBodyType;
		void* Rigidbody3DComponent_SetBodyType;
		void* Rigidbody3DComponent_GetMass;
		void* Rigidbody3DComponent_SetMass;
		void* Rigidbody3DComponent_GetAngularVelocity;
		void* Rigidbody3DComponent_SetAngularVelocity;
		void* Rigidbody3DComponent_GetUseGravity;
		void* Rigidbody3DComponent_SetUseGravity;
		void* Rigidbody3DComponent_GetFriction;
		void* Rigidbody3DComponent_SetFriction;
		void* Rigidbody3DComponent_GetRestitution;
		void* Rigidbody3DComponent_SetRestitution;

		void* BoxCollider3DComponent_GetSize;
		void* BoxCollider3DComponent_SetSize;
		void* BoxCollider3DComponent_GetOffset;
		void* BoxCollider3DComponent_SetOffset;
		void* BoxCollider3DComponent_GetIsTrigger;
		void* BoxCollider3DComponent_SetIsTrigger;
		void* BoxCollider3DComponent_GetEnabled;
		void* BoxCollider3DComponent_SetEnabled;

		void* SphereCollider3DComponent_GetRadius;
		void* SphereCollider3DComponent_SetRadius;
		void* SphereCollider3DComponent_GetOffset;
		void* SphereCollider3DComponent_SetOffset;
		void* SphereCollider3DComponent_GetIsTrigger;
		void* SphereCollider3DComponent_SetIsTrigger;
		void* SphereCollider3DComponent_GetEnabled;
		void* SphereCollider3DComponent_SetEnabled;

		void* CapsuleCollider3DComponent_GetRadius;
		void* CapsuleCollider3DComponent_SetRadius;
		void* CapsuleCollider3DComponent_GetHeight;
		void* CapsuleCollider3DComponent_SetHeight;
		void* CapsuleCollider3DComponent_GetOffset;
		void* CapsuleCollider3DComponent_SetOffset;
		void* CapsuleCollider3DComponent_GetIsTrigger;
		void* CapsuleCollider3DComponent_SetIsTrigger;
		void* CapsuleCollider3DComponent_GetEnabled;
		void* CapsuleCollider3DComponent_SetEnabled;

		void* MeshCollider3DComponent_GetMesh;
		void* MeshCollider3DComponent_SetMesh;
		void* MeshCollider3DComponent_GetOffset;
		void* MeshCollider3DComponent_SetOffset;
		void* MeshCollider3DComponent_GetIsTrigger;
		void* MeshCollider3DComponent_SetIsTrigger;
		void* MeshCollider3DComponent_GetEnabled;
		void* MeshCollider3DComponent_SetEnabled;
	};

	/// Component type name to HasComponent check mapping
	static std::unordered_map<std::string, std::function<bool(Entity)>> s_EntityHasComponentFunctions{};

	// ====================================================================
	// Native callback implementations (called from C# via function pointers)
	// ====================================================================

	static void NativeLog(const char* message)
	{
		if (message)
			Log::CoreInfo("C# Log: {0}", message);
	}

	static uint8_t Entity_HasComponent(const uint64_t entityID, const char* componentTypeName)
	{
		KBR_TRACY_FUNCTION();

		if (!componentTypeName)
			return 0;

		if (const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock())
		{
			const Entity entity = scene->GetEntityByUUID(UUID(entityID));
			const std::string typeName(componentTypeName);

			if (s_EntityHasComponentFunctions.contains(typeName))
				return s_EntityHasComponentFunctions.at(typeName)(entity) ? 1 : 0;
		}
		return 0;
	}

	static auto AddComponentTypeByName(const std::string& qualifiedTypeName) -> std::function<void(Entity)>
	{
		KBR_TRACY_FUNCTION();

		const std::string typeName = qualifiedTypeName.substr(qualifiedTypeName.find_last_of('.') + 1);

		if (typeName == "SpriteRendererComponent")
			return [](Entity entity) { return entity.AddComponent<SpriteRendererComponent>(); };
		if (typeName == "CameraComponent")
			return [](Entity entity) { return entity.AddComponent<CameraComponent>(); };
		if (typeName == "StaticMeshComponent")
			return [](Entity entity) { return entity.AddComponent<StaticMeshComponent>(); };
		if (typeName == "DirectionalLightComponent")
			return [](Entity entity) { return entity.AddComponent<DirectionalLightComponent>(); };
		if (typeName == "PointLightComponent")
			return [](Entity entity) { return entity.AddComponent<PointLightComponent>(); };
		if (typeName == "SpotLightComponent")
			return [](Entity entity) { return entity.AddComponent<SpotLightComponent>(); };
		if (typeName == "EnvironmentComponent")
			return [](Entity entity) { return entity.AddComponent<EnvironmentComponent>(); };
		if (typeName == "TextComponent")
			return [](Entity entity) { return entity.AddComponent<TextComponent>(); };
		if (typeName == "RigidBody3DComponent")
			return [](Entity entity) { return entity.AddComponent<RigidBody3DComponent>(); };
		if (typeName == "BoxCollider3DComponent")
			return [](Entity entity) { return entity.AddComponent<BoxCollider3DComponent>(); };
		if (typeName == "SphereCollider3DComponent")
			return [](Entity entity) { return entity.AddComponent<SphereCollider3DComponent>(); };
		if (typeName == "CapsuleCollider3DComponent")
			return [](Entity entity) { return entity.AddComponent<CapsuleCollider3DComponent>(); };
		if (typeName == "MeshCollider3DComponent")
			return [](Entity entity) { return entity.AddComponent<MeshCollider3DComponent>(); };
		if (typeName == "AudioSource2DComponent")
			return [](Entity entity) { return entity.AddComponent<AudioSource2DComponent>(); };
		if (typeName == "AudioSource3DComponent")
			return [](Entity entity) { return entity.AddComponent<AudioSource3DComponent>(); };
		if (typeName == "AudioListenerComponent")
			return [](Entity entity) { return entity.AddComponent<AudioListenerComponent>(); };
		if (typeName == "AnimatorComponent")
			return [](Entity entity) { return entity.AddComponent<AnimatorComponent>(); };
		if (typeName == "SocketComponent")
			return [](Entity entity) { return entity.AddComponent<SocketComponent>(); };
		if (typeName == "RagdollComponent")
			return [](Entity entity) { return entity.AddComponent<RagdollComponent>(); };

		KBRAssert(false, "Unknown component type: {0}", typeName);
		return nullptr;
	}

	static void Entity_AddComponent(const uint64_t entityID, const char* componentTypeName)
	{
		KBR_TRACY_FUNCTION();

		if (!componentTypeName)
			return;

		if (const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock())
		{
			const Entity entity = scene->GetEntityByUUID(UUID(entityID));
			const std::string typeName(componentTypeName);

			if (const auto addComponentFunc = AddComponentTypeByName(typeName))
			{
				addComponentFunc(entity);
			}
		}
	}

	static uint64_t Entity_FindEntityByName(const char* name)
	{
		KBR_TRACY_FUNCTION();

		if (!name)
			return UUID::Invalid();

		const std::string nameStr(name);

		if (const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock())
		{
			if (const Entity entity = scene->FindEntityByName(nameStr))
			{
				return entity.GetUUID();
			}
		}
		return UUID::Invalid();
	}

	static uint64_t Entity_Instantiate(const char* name)
	{
		KBR_TRACY_FUNCTION();

		const std::string nameStr = name ? std::string(name) : "Entity";
		if (const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock())
		{
			const Entity entity = scene->CreateEntity(nameStr);
			return entity.GetUUID();
		}
		return UUID::Invalid();
	}

	static uint64_t Entity_InstantiatePrefab(const uint64_t prefabHandle)
	{
		KBR_TRACY_FUNCTION();

		if (const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock())
		{
			const Entity entity = scene->InstantiatePrefab(AssetHandle(prefabHandle));
			if (!entity)
				return UUID::Invalid();

			return entity.GetUUID();
		}
		return UUID::Invalid();
	}

	static void TransformComponent_GetTranslation(const uint64_t entityID, glm::vec3* outTranslation)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outTranslation)
		{
			const glm::vec3 translation = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TransformComponent>().Translation;
			*outTranslation = translation;
		}
	}

	static void TransformComponent_SetTranslation(const uint64_t entityID, const glm::vec3* translation)
	{
		KBR_TRACY_FUNCTION();

		if (translation)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			const auto& ownedScene = scene.lock();
			if (!ownedScene)
			{
				Kerberos::Log::CoreError("Failed to set translation for entity ID: {}. Scene context is not valid.", entityID);
				return;
			}

			const Entity entity = ownedScene->GetEntityByUUID(UUID(entityID));
			glm::vec3& currentTranslation = entity.GetComponent<TransformComponent>().Translation;
			currentTranslation = *translation;
			if (entity.HasComponent<RigidBody3DComponent>())
			{
				entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
			}
			ownedScene->CalculateEntityTransform(entity);
		}
	}

	static void TransformComponent_GetRotation(const uint64_t entityID, glm::vec3* outRotation)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outRotation)
		{
			const glm::vec3 rotation = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TransformComponent>().Rotation;
			*outRotation = rotation;
		}
	}

	static void TransformComponent_SetRotation(const uint64_t entityID, const glm::vec3* rotation)
	{
		KBR_TRACY_FUNCTION();

		if (rotation)
		{
			const WeakRef<Scene>& scene = ScriptEngine::GetSceneContext();
			const auto& ownedScene = scene.lock();
			if (!ownedScene)
			{
				Kerberos::Log::CoreError("Failed to set rotation for entity ID: {}. Scene context is not valid.", entityID);
				return;
			}

			const Entity entity = ownedScene->GetEntityByUUID(UUID(entityID));
			glm::vec3& currentRotation = entity.GetComponent<TransformComponent>().Rotation;
			currentRotation = *rotation;
			if (entity.HasComponent<RigidBody3DComponent>())
			{
				entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
			}
			ownedScene->CalculateEntityTransform(entity);
		}
	}

	static void TransformComponent_GetScale(const uint64_t entityID, glm::vec3* outScale)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outScale)
		{
			const glm::vec3 scale = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TransformComponent>().Scale;
			*outScale = scale;
		}
	}

	static void TransformComponent_SetScale(const uint64_t entityID, const glm::vec3* scale)
	{
		KBR_TRACY_FUNCTION();

		if (scale)
		{
			const WeakRef<Scene>& scene = ScriptEngine::GetSceneContext();
			const auto& ownedScene = scene.lock();
			if (!ownedScene)
			{
				Kerberos::Log::CoreError("Failed to set scale for entity ID: {}. Scene context is not valid.", entityID);
				return;
			}

			const Entity entity = ownedScene->GetEntityByUUID(UUID(entityID));
			glm::vec3& currentScale = entity.GetComponent<TransformComponent>().Scale;
			currentScale = *scale;
			if (entity.HasComponent<RigidBody3DComponent>())
			{
				entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
			}
			ownedScene->CalculateEntityTransform(entity);
		}
	}

	static void StaticMeshComponent_GetMesh(const uint64_t entityID, uint64_t* outMesh)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Ref<Scene> currentScene = scene.lock();
		const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

		KBRAssert(entity.HasComponent<StaticMeshComponent>(), "Entity doesn't have a StaticMeshComponent.");

		const StaticMeshComponent& staticMeshComponent = entity.GetComponent<StaticMeshComponent>();
		*outMesh = staticMeshComponent.StaticMesh->GetHandle();
	}

	static void StaticMeshComponent_SetMesh(const uint64_t entityID, const uint64_t meshRef)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Ref<Scene> currentScene = scene.lock();
		const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

		KBRAssert(entity.HasComponent<StaticMeshComponent>(), "Entity doesn't have a StaticMeshComponent.");
		// TODO: when using asset handles for assets, simply set it, maybe check if correct

		StaticMeshComponent& staticMeshComponent = entity.GetComponent<StaticMeshComponent>();
		const Ref<Mesh> mesh = AssetManager::AsAsset<Mesh>(AssetHandle(meshRef));
		if (!mesh)
		{
			Kerberos::Log::CoreWarn("Failed to set static mesh for entity ID: {}, mesh asset ID: {}", entityID, meshRef);
		}
		staticMeshComponent.StaticMesh = mesh;
	}

	static void Rigidbody3DComponent_GetVelocity(const uint64_t entityID, glm::vec3* outVelocity)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outVelocity)
		{
			const glm::vec3 velocity = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<RigidBody3DComponent>().Velocity;
			*outVelocity = velocity;
		}
	}

	static void Rigidbody3DComponent_SetVelocity(const uint64_t entityID, const glm::vec3* velocity)
	{
		KBR_TRACY_FUNCTION();

		if (velocity)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			const Ref<Scene> currentScene = scene.lock();
			if (!currentScene)
			{
				Kerberos::Log::CoreError("Failed to set rigidbody velocity for entity ID: {}. Scene context is not valid.", entityID);
				return;
			}

			const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));
			RigidBody3DComponent& rb = entity.GetComponent<RigidBody3DComponent>();
			glm::vec3& currentVelocity = rb.Velocity;
			currentVelocity = *velocity;

			if (rb.RuntimeBody)
			{
				const JPH::Body* body = static_cast<JPH::Body*>(rb.RuntimeBody);
				const uint32_t bodyIdValue = body->GetID().GetIndexAndSequenceNumber();
				currentScene->GetPhysicsSystem().SetLinearVelocity(bodyIdValue, *velocity);
			}
		}
	}

	static void Rigidbody3DComponent_ApplyImpulse(const uint64_t entityID, const glm::vec3* force)
	{
		KBR_TRACY_FUNCTION();

		if (force)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			const Ref<Scene> currentScene = scene.lock();
			const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

			KBRAssert(entity.HasComponent<RigidBody3DComponent>(), "Entity doesn't have a Rigidbody3DComponent.");

			const RigidBody3DComponent& rb3d = entity.GetComponent<RigidBody3DComponent>();
			KBRAssert(rb3d.RuntimeBody != nullptr, "Rigidbody3DComponent doesn't have a runtime body.");

			const JPH::Body* body = static_cast<JPH::Body*>(rb3d.RuntimeBody);
			const JPH::BodyID& bodyId = body->GetID();
			const uint32_t bodyIdValue = bodyId.GetIndexAndSequenceNumber();

			const PhysicsSystem& physicsSystem = currentScene->GetPhysicsSystem();
			physicsSystem.AddImpulse(bodyIdValue, *force);
		}
	}

	static void Rigidbody3DComponent_ApplyImpulseAtPoint(const uint64_t entityID, const glm::vec3* force, const glm::vec3* inPoint)
	{
		KBR_TRACY_FUNCTION();

		if (force)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			const Ref<Scene> currentScene = scene.lock();
			const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

			KBRAssert(entity.HasComponent<RigidBody3DComponent>(), "Entity doesn't have a Rigidbody3DComponent.");

			const RigidBody3DComponent& rb3D = entity.GetComponent<RigidBody3DComponent>();
			KBRAssert(rb3D.RuntimeBody != nullptr, "Rigidbody3DComponent doesn't have a runtime body.");

			const JPH::Body* body = static_cast<JPH::Body*>(rb3D.RuntimeBody);
			const JPH::BodyID& bodyId = body->GetID();
			const uint32_t bodyIdValue = bodyId.GetIndexAndSequenceNumber();

			const PhysicsSystem& physicsSystem = currentScene->GetPhysicsSystem();
			physicsSystem.AddImpulse(bodyIdValue, *force, *inPoint);
		}
	}

	// ====================================================================
	// Animation
	// ====================================================================

	static AnimatorComponent* GetAnimatorComponent(const uint64_t entityID)
	{
		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return nullptr;

		const Entity entity = scene->TryGetEntityByUUID(UUID(entityID));
		if (!entity || !entity.HasComponent<AnimatorComponent>())
			return nullptr;

		return &entity.GetComponent<AnimatorComponent>();
	}

	static Ref<AnimationClip> ResolveAnimatorClip(const AnimatorComponent& animator, const int32_t clipIndex)
	{
		if (clipIndex < 0 || static_cast<size_t>(clipIndex) >= animator.AnimationClips.size())
			return nullptr;

		const size_t index = static_cast<size_t>(clipIndex);
		if (index < animator.LoadedClips.size() && animator.LoadedClips[index])
			return animator.LoadedClips[index];

		const AssetHandle handle = animator.AnimationClips[index];
		return handle.IsValid() ? AssetManager::AsAsset<AnimationClip>(handle) : nullptr;
	}

	static void ClearPendingCrossFade(AnimatorComponent& animator)
	{
		animator.PendingCrossFadeClip = -1;
		animator.PendingCrossFadeDuration = 0.0f;
		animator.PendingCrossFadeRestart = true;
	}

	static void AnimatorComponent_Play(const uint64_t entityID, const int32_t clipIndex)
	{
		KBR_TRACY_FUNCTION();

		AnimatorComponent* animator = GetAnimatorComponent(entityID);
		if (!animator)
		{
			Log::CoreWarn("AnimatorComponent_Play: entity {} has no animator", entityID);
			return;
		}

		ClearPendingCrossFade(*animator);

		bool timeReset = false;

		if (clipIndex >= 0)
		{
			if (static_cast<size_t>(clipIndex) >= animator->AnimationClips.size())
			{
				Log::CoreWarn("AnimatorComponent_Play: clip index {} is out of range on entity {}",
				              clipIndex, entityID);
				return;
			}

			animator->ActiveClip = clipIndex;
			animator->CurrentTime = 0.0f;
			timeReset = true;
		}
		else if (animator->ActiveClip < 0 && !animator->AnimationClips.empty())
		{
			animator->ActiveClip = 0;
			animator->CurrentTime = 0.0f;
			timeReset = true;
		}

		if (timeReset)
			animator->PlaybackRevision++;

		animator->IsPlaying = true;
	}

	static void AnimatorComponent_PlayByName(const uint64_t entityID, const char* clipName)
	{
		KBR_TRACY_FUNCTION();

		if (!clipName)
			return;

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		if (!animator)
		{
			Log::CoreWarn("AnimatorComponent_PlayByName: entity {} has no animator", entityID);
			return;
		}

		for (size_t clipIndex = 0; clipIndex < animator->AnimationClips.size(); ++clipIndex)
		{
			const Ref<AnimationClip> clip = ResolveAnimatorClip(*animator, static_cast<int32_t>(clipIndex));
			if (clip && clip->GetName() == clipName)
			{
				AnimatorComponent_Play(entityID, static_cast<int32_t>(clipIndex));
				return;
			}
		}

		Log::CoreWarn("AnimatorComponent_PlayByName: clip '{}' was not found on entity {}", clipName, entityID);
	}

	static void AnimatorComponent_Stop(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		if (AnimatorComponent* animator = GetAnimatorComponent(entityID))
		{
			ClearPendingCrossFade(*animator);
			animator->IsPlaying = false;
			animator->CurrentTime = 0.0f;
            animator->PlaybackRevision++;
		}
	}

	static void AnimatorComponent_Pause(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		if (AnimatorComponent* animator = GetAnimatorComponent(entityID))
			animator->IsPlaying = false;
	}

	static float AnimatorComponent_GetTime(const uint64_t entityID)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		return animator ? animator->CurrentTime : 0.0f;
	}

	static void AnimatorComponent_SetTime(const uint64_t entityID, const float time)
	{
        KBR_TRACY_FUNCTION();

		AnimatorComponent* animator = GetAnimatorComponent(entityID);
		if (!animator || !std::isfinite(time))
			return;

		animator->CurrentTime = std::max(0.0f, time);

		if (const Ref<AnimationClip> clip = ResolveAnimatorClip(*animator, animator->ActiveClip);
			clip && clip->GetDuration() > 0.0f)
		{
			animator->CurrentTime = std::min(animator->CurrentTime, clip->GetDuration());
		}

		animator->PlaybackRevision++;
	}

	static float AnimatorComponent_GetSpeed(const uint64_t entityID)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		return animator ? animator->PlaybackSpeed : 1.0f;
	}

	static void AnimatorComponent_SetSpeed(const uint64_t entityID, const float speed)
	{
        KBR_TRACY_FUNCTION();

		AnimatorComponent* animator = GetAnimatorComponent(entityID);
        if (animator && std::isfinite(speed))
			animator->PlaybackSpeed = speed;
	}

	static uint8_t AnimatorComponent_IsPlaying(const uint64_t entityID)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		return animator && animator->IsPlaying ? 1 : 0;
	}

	static void AnimatorComponent_SetPlaying(const uint64_t entityID, const uint8_t playing)
	{
        KBR_TRACY_FUNCTION();

		if (AnimatorComponent* animator = GetAnimatorComponent(entityID))
			animator->IsPlaying = playing != 0;
	}

	static uint8_t AnimatorComponent_IsLooping(const uint64_t entityID)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		return animator && animator->IsLooping ? 1 : 0;
	}

	static void AnimatorComponent_SetLooping(const uint64_t entityID, const uint8_t looping)
	{
        KBR_TRACY_FUNCTION();

		if (AnimatorComponent* animator = GetAnimatorComponent(entityID))
			animator->IsLooping = looping != 0;
	}

	static uint8_t AnimatorComponent_GetPlayOnStart(const uint64_t entityID)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		return animator && animator->PlayOnStart ? 1 : 0;
	}

	static void AnimatorComponent_SetPlayOnStart(const uint64_t entityID, const uint8_t playOnStart)
	{
        KBR_TRACY_FUNCTION();

		if (AnimatorComponent* animator = GetAnimatorComponent(entityID))
			animator->PlayOnStart = playOnStart != 0;
	}

	static int32_t AnimatorComponent_GetActiveClip(const uint64_t entityID)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		return animator ? animator->ActiveClip : -1;
	}

	static void AnimatorComponent_SetActiveClip(const uint64_t entityID, const int32_t clipIndex)
	{
        KBR_TRACY_FUNCTION();

		AnimatorComponent* animator = GetAnimatorComponent(entityID);
		if (!animator)
			return;

		if (clipIndex < -1 || static_cast<size_t>(clipIndex) >= animator->AnimationClips.size())
		{
			Log::CoreWarn("AnimatorComponent_SetActiveClip: clip index {} is out of range on entity {}",
			              clipIndex, entityID);
			return;
		}

		ClearPendingCrossFade(*animator);
		animator->ActiveClip = clipIndex;
		animator->CurrentTime = 0.0f;
        animator->PlaybackRevision++;
	}

	static void AnimatorComponent_CrossFade(const uint64_t entityID, const int32_t clipIndex,
	                                         const float duration, const uint8_t restart)
	{
		KBR_TRACY_FUNCTION();

		AnimatorComponent* animator = GetAnimatorComponent(entityID);
		if (!animator)
		{
			Log::CoreWarn("AnimatorComponent_CrossFade: entity {} has no animator", entityID);
			return;
		}

		if (clipIndex < 0 || static_cast<size_t>(clipIndex) >= animator->AnimationClips.size())
		{
			Log::CoreWarn("AnimatorComponent_CrossFade: clip index {} is out of range on entity {}",
			              clipIndex, entityID);
			return;
		}

		animator->PendingCrossFadeClip = clipIndex;
		animator->PendingCrossFadeDuration =
			std::isfinite(duration) ? std::max(0.0f, duration) : 0.0f;
		animator->PendingCrossFadeRestart = restart != 0;

		// Keep the authored playback state in sync with the request. The runtime consumes the
		// pending fields on the next clock advance or pose evaluation.
		animator->ActiveClip = clipIndex;
		if (restart != 0)
			animator->CurrentTime = 0.0f;
		animator->IsPlaying = true;
		animator->PlaybackRevision++;
	}

	static void AnimatorComponent_CrossFadeByName(const uint64_t entityID, const char* clipName,
	                                               const float duration, const uint8_t restart)
	{
		KBR_TRACY_FUNCTION();

		if (!clipName)
			return;

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		if (!animator)
		{
			Log::CoreWarn("AnimatorComponent_CrossFadeByName: entity {} has no animator", entityID);
			return;
		}

		for (size_t clipIndex = 0; clipIndex < animator->AnimationClips.size(); ++clipIndex)
		{
			const Ref<AnimationClip> clip = ResolveAnimatorClip(*animator, static_cast<int32_t>(clipIndex));
			if (clip && clip->GetName() == clipName)
			{
				AnimatorComponent_CrossFade(entityID, static_cast<int32_t>(clipIndex), duration, restart);
				return;
			}
		}

		Log::CoreWarn("AnimatorComponent_CrossFadeByName: clip '{}' was not found on entity {}",
		              clipName, entityID);
	}

	static int32_t AnimatorComponent_GetClipCount(const uint64_t entityID)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		return animator ? static_cast<int32_t>(animator->AnimationClips.size()) : 0;
	}

	static float AnimatorComponent_GetClipDuration(const uint64_t entityID, const int32_t clipIndex)
	{
        KBR_TRACY_FUNCTION();

		const AnimatorComponent* animator = GetAnimatorComponent(entityID);
		if (!animator)
			return 0.0f;

		const Ref<AnimationClip> clip = ResolveAnimatorClip(*animator, clipIndex);
		return clip ? clip->GetDuration() : 0.0f;
	}

	static void Socket_Attach(const uint64_t entityID, const uint64_t skeletonEntityID, const char* jointName,
	                          const glm::vec3* localTranslation, const glm::vec3* localRotation,
	                          const glm::vec3* localScale)
	{
		KBR_TRACY_FUNCTION();

		if (!jointName || jointName[0] == '\0')
		{
			Log::CoreWarn("Socket_Attach: entity {} was given an empty joint name", entityID);
			return;
		}

		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return;

		Entity entity = scene->TryGetEntityByUUID(UUID(entityID));
		const Entity skeletonEntity = scene->TryGetEntityByUUID(UUID(skeletonEntityID));
		if (!entity || !skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>())
		{
			Log::CoreWarn("Socket_Attach: entity {} or skeleton entity {} is invalid", entityID, skeletonEntityID);
			return;
		}

		const SkeletonComponent& skeletonComponent = skeletonEntity.GetComponent<SkeletonComponent>();
		if (skeletonComponent.Skeleton && skeletonComponent.Skeleton->GetJointIndex(jointName) < 0)
		{
			Log::CoreWarn("Socket_Attach: skeleton entity {} has no joint named '{}'", skeletonEntityID, jointName);
			return;
		}

		SocketComponent& socket = entity.HasComponent<SocketComponent>()
			? entity.GetComponent<SocketComponent>()
			: entity.AddComponent<SocketComponent>();

		socket.SkeletonEntity = UUID(skeletonEntityID);
		socket.JointName = jointName;
		socket.LocalTranslation = localTranslation ? *localTranslation : glm::vec3(0.0f);
		socket.LocalRotation = localRotation ? *localRotation : glm::vec3(0.0f);
		socket.LocalScale = localScale ? *localScale : glm::vec3(1.0f);
	}

	static void Socket_Detach(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return;

		Entity entity = scene->TryGetEntityByUUID(UUID(entityID));
		if (entity && entity.HasComponent<SocketComponent>())
		{
			entity.RemoveComponent<SocketComponent>();

			// Restore the authored hierarchy transform instead of leaving the last socket world
			// transform until the next frame's transform pass.
			scene->CalculateEntityTransform(entity);
		}
	}

	static RagdollComponent* GetRagdollComponent(const uint64_t entityID)
	{
		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return nullptr;

		const Entity entity = scene->TryGetEntityByUUID(UUID(entityID));
		if (!entity || !entity.HasComponent<RagdollComponent>())
			return nullptr;

		return &entity.GetComponent<RagdollComponent>();
	}

	static RagdollSystem* GetRagdollSystem()
	{
		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		return scene ? &scene->GetRagdollSystem() : nullptr;
	}

	static void RagdollComponent_EnableRagdoll(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();
		if (RagdollSystem* ragdoll = GetRagdollSystem())
			ragdoll->EnableRagdoll(UUID(entityID));
	}

	static void RagdollComponent_DisableRagdoll(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();
		if (RagdollSystem* ragdoll = GetRagdollSystem())
			ragdoll->DisableRagdoll(UUID(entityID));
	}

	static uint8_t RagdollComponent_IsRagdoll(const uint64_t entityID)
	{
		const RagdollSystem* ragdoll = GetRagdollSystem();
		return ragdoll && ragdoll->IsRagdoll(UUID(entityID)) ? 1 : 0;
	}

	static int32_t RagdollComponent_GetState(const uint64_t entityID)
	{
		const RagdollSystem* ragdoll = GetRagdollSystem();
		return ragdoll ? static_cast<int32_t>(ragdoll->GetState(UUID(entityID))) : 0;
	}

	static float RagdollComponent_GetWeight(const uint64_t entityID)
	{
		const RagdollSystem* ragdoll = GetRagdollSystem();
		return ragdoll ? ragdoll->GetBlendWeight(UUID(entityID)) : 0.0f;
	}

	static float RagdollComponent_GetBlendDuration(const uint64_t entityID)
	{
		const RagdollComponent* ragdoll = GetRagdollComponent(entityID);
		return ragdoll ? ragdoll->BlendDuration : 0.0f;
	}

	static void RagdollComponent_SetBlendDuration(const uint64_t entityID, const float duration)
	{
		RagdollComponent* ragdoll = GetRagdollComponent(entityID);
		if (ragdoll && std::isfinite(duration))
			ragdoll->BlendDuration = std::max(0.0f, duration);
	}

	static uint8_t RagdollComponent_GetStartInRagdoll(const uint64_t entityID)
	{
		const RagdollComponent* ragdoll = GetRagdollComponent(entityID);
		return ragdoll && ragdoll->StartInRagdoll ? 1 : 0;
	}

	static void RagdollComponent_SetStartInRagdoll(const uint64_t entityID, const uint8_t startInRagdoll)
	{
        if (RagdollComponent* ragdoll = GetRagdollComponent(entityID))
			ragdoll->StartInRagdoll = startInRagdoll != 0;
	}

	static float RagdollComponent_GetJointRadiusScale(const uint64_t entityID)
	{
		const RagdollComponent* ragdoll = GetRagdollComponent(entityID);
		return ragdoll ? ragdoll->JointRadiusScale : 1.0f;
	}

	static void RagdollComponent_SetJointRadiusScale(const uint64_t entityID, const float scale)
	{
		RagdollComponent* ragdoll = GetRagdollComponent(entityID);
		if (ragdoll && std::isfinite(scale))
			ragdoll->JointRadiusScale = std::max(0.01f, scale);
	}

	template<typename T>
	static T* GetPhysicsComponent(const uint64_t entityID)
	{
		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return nullptr;

		const Entity entity = scene->TryGetEntityByUUID(UUID(entityID));
		if (!entity || !entity.HasComponent<T>())
			return nullptr;

		return &entity.GetComponent<T>();
	}

	static void MarkPhysicsBodyDirty(const uint64_t entityID)
	{
		if (RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID))
			rigidBody->IsDirty = true;
	}

	static void Rigidbody3DComponent_SetBodyType(const uint64_t entityID, const int32_t bodyType)
	{
		RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		if (!rigidBody || bodyType < 0 ||
			bodyType > static_cast<int32_t>(RigidBody3DComponent::BodyType::Kinematic))
		{
			return;
		}

		rigidBody->Type = static_cast<RigidBody3DComponent::BodyType>(bodyType);
		rigidBody->IsDirty = true;
	}

	static int32_t Rigidbody3DComponent_GetBodyType(const uint64_t entityID)
	{
		const RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		return rigidBody ? static_cast<int32_t>(rigidBody->Type) : 0;
	}

	static void Rigidbody3DComponent_SetMass(const uint64_t entityID, const float mass)
	{
		RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		if (!rigidBody || !std::isfinite(mass) || mass <= 0.0f)
			return;

		rigidBody->Mass = mass;
		rigidBody->IsDirty = true;
	}

	static float Rigidbody3DComponent_GetMass(const uint64_t entityID)
	{
		const RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		return rigidBody ? rigidBody->Mass : 0.0f;
	}

	static void Rigidbody3DComponent_SetAngularVelocity(const uint64_t entityID, const glm::vec3* velocity)
	{
		if (!velocity)
			return;

		RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		if (!rigidBody)
			return;

		rigidBody->AngularVelocity = *velocity;

		if (rigidBody->RuntimeBody)
		{
			if (const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock())
			{
				const JPH::Body* body = static_cast<JPH::Body*>(rigidBody->RuntimeBody);
				scene->GetPhysicsSystem().SetAngularVelocity(body->GetID().GetIndexAndSequenceNumber(), *velocity);
			}
		}
	}

	static void Rigidbody3DComponent_GetAngularVelocity(const uint64_t entityID, glm::vec3* outVelocity)
	{
		if (!outVelocity)
			return;

		const RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		*outVelocity = rigidBody ? rigidBody->AngularVelocity : glm::vec3(0.0f);
	}

	static void Rigidbody3DComponent_SetUseGravity(const uint64_t entityID, const uint8_t useGravity)
	{
		RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		if (!rigidBody)
			return;

		rigidBody->UseGravity = useGravity != 0;
		rigidBody->IsDirty = true;
	}

	static uint8_t Rigidbody3DComponent_GetUseGravity(const uint64_t entityID)
	{
		const RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		return rigidBody && rigidBody->UseGravity ? 1 : 0;
	}

	static void Rigidbody3DComponent_SetFriction(const uint64_t entityID, const float friction)
	{
		RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		if (!rigidBody || !std::isfinite(friction))
			return;

		rigidBody->Friction = std::clamp(friction, 0.0f, 1.0f);
		rigidBody->IsDirty = true;
	}

	static float Rigidbody3DComponent_GetFriction(const uint64_t entityID)
	{
		const RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		return rigidBody ? rigidBody->Friction : 0.0f;
	}

	static void Rigidbody3DComponent_SetRestitution(const uint64_t entityID, const float restitution)
	{
		RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		if (!rigidBody || !std::isfinite(restitution))
			return;

		rigidBody->Restitution = std::clamp(restitution, 0.0f, 1.0f);
		rigidBody->IsDirty = true;
	}

	static float Rigidbody3DComponent_GetRestitution(const uint64_t entityID)
	{
		const RigidBody3DComponent* rigidBody = GetPhysicsComponent<RigidBody3DComponent>(entityID);
		return rigidBody ? rigidBody->Restitution : 0.0f;
	}

	static void BoxCollider3DComponent_SetSize(const uint64_t entityID, const glm::vec3* size)
	{
		if (BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID))
		{
			collider->Size = *size;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static void BoxCollider3DComponent_GetSize(const uint64_t entityID, glm::vec3* outSize)
	{
		if (!outSize) return;
		const BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID);
		*outSize = collider ? collider->Size : glm::vec3(1.0f);
	}

	static void BoxCollider3DComponent_SetOffset(const uint64_t entityID, const glm::vec3* offset)
	{
		if (BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID))
		{
			collider->Offset = *offset;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static void BoxCollider3DComponent_GetOffset(const uint64_t entityID, glm::vec3* outOffset)
	{
		if (!outOffset) return;
		const BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID);
		*outOffset = collider ? collider->Offset : glm::vec3(0.0f);
	}

	static void BoxCollider3DComponent_SetIsTrigger(const uint64_t entityID, const uint8_t isTrigger)
	{
		if (BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID))
		{
			collider->IsTrigger = isTrigger != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t BoxCollider3DComponent_GetIsTrigger(const uint64_t entityID)
	{
		const BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID);
		return collider && collider->IsTrigger ? 1 : 0;
	}

	static void BoxCollider3DComponent_SetEnabled(const uint64_t entityID, const uint8_t enabled)
	{
		if (BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID))
		{
			collider->Enabled = enabled != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t BoxCollider3DComponent_GetEnabled(const uint64_t entityID)
	{
		const BoxCollider3DComponent* collider = GetPhysicsComponent<BoxCollider3DComponent>(entityID);
		return collider && collider->Enabled ? 1 : 0;
	}

	static void SphereCollider3DComponent_SetRadius(const uint64_t entityID, const float radius)
	{
		if (SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID))
		{
			collider->Radius = radius;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static float SphereCollider3DComponent_GetRadius(const uint64_t entityID)
	{
		const SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID);
		return collider ? collider->Radius : 0.0f;
	}

	static void SphereCollider3DComponent_SetOffset(const uint64_t entityID, const glm::vec3* offset)
	{
		if (SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID))
		{
			collider->Offset = *offset;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static void SphereCollider3DComponent_GetOffset(const uint64_t entityID, glm::vec3* outOffset)
	{
		if (!outOffset) return;
		const SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID);
		*outOffset = collider ? collider->Offset : glm::vec3(0.0f);
	}

	static void SphereCollider3DComponent_SetIsTrigger(const uint64_t entityID, const uint8_t isTrigger)
	{
		if (SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID))
		{
			collider->IsTrigger = isTrigger != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t SphereCollider3DComponent_GetIsTrigger(const uint64_t entityID)
	{
		const SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID);
		return collider && collider->IsTrigger ? 1 : 0;
	}

	static void SphereCollider3DComponent_SetEnabled(const uint64_t entityID, const uint8_t enabled)
	{
		if (SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID))
		{
			collider->Enabled = enabled != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t SphereCollider3DComponent_GetEnabled(const uint64_t entityID)
	{
		const SphereCollider3DComponent* collider = GetPhysicsComponent<SphereCollider3DComponent>(entityID);
		return collider && collider->Enabled ? 1 : 0;
	}

	static void CapsuleCollider3DComponent_SetRadius(const uint64_t entityID, const float radius)
	{
		if (CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID))
		{
			collider->Radius = radius;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static float CapsuleCollider3DComponent_GetRadius(const uint64_t entityID)
	{
		const CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID);
		return collider ? collider->Radius : 0.0f;
	}

	static void CapsuleCollider3DComponent_SetHeight(const uint64_t entityID, const float height)
	{
		if (CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID))
		{
			collider->Height = height;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static float CapsuleCollider3DComponent_GetHeight(const uint64_t entityID)
	{
		const CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID);
		return collider ? collider->Height : 0.0f;
	}

	static void CapsuleCollider3DComponent_SetOffset(const uint64_t entityID, const glm::vec3* offset)
	{
		if (CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID))
		{
			collider->Offset = *offset;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static void CapsuleCollider3DComponent_GetOffset(const uint64_t entityID, glm::vec3* outOffset)
	{
		if (!outOffset) return;
		const CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID);
		*outOffset = collider ? collider->Offset : glm::vec3(0.0f);
	}

	static void CapsuleCollider3DComponent_SetIsTrigger(const uint64_t entityID, const uint8_t isTrigger)
	{
		if (CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID))
		{
			collider->IsTrigger = isTrigger != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t CapsuleCollider3DComponent_GetIsTrigger(const uint64_t entityID)
	{
		const CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID);
		return collider && collider->IsTrigger ? 1 : 0;
	}

	static void CapsuleCollider3DComponent_SetEnabled(const uint64_t entityID, const uint8_t enabled)
	{
		if (CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID))
		{
			collider->Enabled = enabled != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t CapsuleCollider3DComponent_GetEnabled(const uint64_t entityID)
	{
		const CapsuleCollider3DComponent* collider = GetPhysicsComponent<CapsuleCollider3DComponent>(entityID);
		return collider && collider->Enabled ? 1 : 0;
	}

	static void MeshCollider3DComponent_SetMesh(const uint64_t entityID, const uint64_t meshHandle)
	{
		MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID);
		if (!collider)
			return;

		collider->Mesh = AssetManager::AsAsset<Mesh>(AssetHandle(meshHandle));
		MarkPhysicsBodyDirty(entityID);
	}

	static void MeshCollider3DComponent_GetMesh(const uint64_t entityID, uint64_t* outMesh)
	{
		if (!outMesh) return;
		const MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID);
		*outMesh = collider && collider->Mesh ? collider->Mesh->GetHandle() : AssetHandle::Invalid();
	}

	static void MeshCollider3DComponent_SetOffset(const uint64_t entityID, const glm::vec3* offset)
	{
		if (MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID))
		{
			collider->Offset = *offset;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static void MeshCollider3DComponent_GetOffset(const uint64_t entityID, glm::vec3* outOffset)
	{
		if (!outOffset) return;
		const MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID);
		*outOffset = collider ? collider->Offset : glm::vec3(0.0f);
	}

	static void MeshCollider3DComponent_SetIsTrigger(const uint64_t entityID, const uint8_t isTrigger)
	{
		if (MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID))
		{
			collider->IsTrigger = isTrigger != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t MeshCollider3DComponent_GetIsTrigger(const uint64_t entityID)
	{
		const MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID);
		return collider && collider->IsTrigger ? 1 : 0;
	}

	static void MeshCollider3DComponent_SetEnabled(const uint64_t entityID, const uint8_t enabled)
	{
		if (MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID))
		{
			collider->Enabled = enabled != 0;
			MarkPhysicsBodyDirty(entityID);
		}
	}

	static uint8_t MeshCollider3DComponent_GetEnabled(const uint64_t entityID)
	{
		const MeshCollider3DComponent* collider = GetPhysicsComponent<MeshCollider3DComponent>(entityID);
		return collider && collider->Enabled ? 1 : 0;
	}

	/// String return buffer (managed side will read from this pointer)
	static std::string s_StringReturnBuffer;

	static const char* TextComponent_GetText(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));
		const TextComponent& textComponent = entity.GetComponent<TextComponent>();
		s_StringReturnBuffer = textComponent.Text;
		return s_StringReturnBuffer.c_str();
	}

	static void TextComponent_SetText(const uint64_t entityID, const char* text)
	{
		KBR_TRACY_FUNCTION();

		if (!text)
			return;

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		TextComponent& textComponent = entity.GetComponent<TextComponent>();
		textComponent.Text = std::string(text);
	}

	static void TextComponent_GetColor(const uint64_t entityID, glm::vec4* outColor)
	{
		KBR_TRACY_FUNCTION();

		if (!outColor)
			return;

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const glm::vec4 color = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().Color;
		*outColor = color;
	}

	static void TextComponent_SetColor(const uint64_t entityID, const glm::vec4* color)
	{
		KBR_TRACY_FUNCTION();

		if (!color)
			return;

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		glm::vec4& currentColor = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().Color;
		currentColor = *color;
	}

	static float TextComponent_GetFontSize(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const float fontSize = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().FontSize;
		return fontSize;
	}

	static void TextComponent_SetFontSize(const uint64_t entityID, const float fontSize)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		float& currentFontSize = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().FontSize;
		currentFontSize = fontSize;
	}

	static const char* TextComponent_GetFontPath(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const TextComponent& textComponent = entity.GetComponent<TextComponent>();
		s_StringReturnBuffer = textComponent.Font->GetFilepath().string();
		return s_StringReturnBuffer.c_str();
	}

	static void TextComponent_SetFontPath([[maybe_unused]] const uint64_t entityID, const char* fontPath)
	{
		KBR_TRACY_FUNCTION();

		if (!fontPath)
			return;

		throw std::runtime_error("TextComponent_SetFontPath is not implemented yet");
	}

	static void AudioSource2DComponent_Play(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Play();
	}

	static void AudioSource2DComponent_Stop(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Stop();
	}

	static float AudioSource2DComponent_GetVolume(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		return audioComponent.Volume;
	}

	static void AudioSource2DComponent_SetVolume(const uint64_t entityID, const float volume)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		audioComponent.Volume = volume;
		audioComponent.SoundAsset->SetVolume(volume);
	}

	static void AudioSource2DComponent_SetLooping(const uint64_t entityID, const uint8_t loop)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		audioComponent.Loop = loop != 0;
	}

	static uint8_t AudioSource2DComponent_IsLooping(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		return audioComponent.Loop ? 1 : 0;
	}

	static void AudioSource3DComponent_Play(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Play();
	}

	static void AudioSource3DComponent_Stop(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Stop();
	}

	static float AudioSource3DComponent_GetVolume(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		return audioComponent.Volume;
	}

	static void AudioSource3DComponent_SetVolume(const uint64_t entityID, const float volume)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		audioComponent.Volume = volume;
		audioComponent.SoundAsset->SetVolume(volume);
	}

	static void AudioSource3DComponent_SetLooping(const uint64_t entityID, const uint8_t loop)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		audioComponent.Loop = loop != 0;
	}

	static uint8_t AudioSource3DComponent_IsLooping(const uint64_t entityID)
	{
		KBR_TRACY_FUNCTION();

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		return audioComponent.Loop ? 1 : 0;
	}

	static uint8_t Physics_Raycast(const glm::vec3* origin, const glm::vec3* direction, const float maxDistance, glm::vec3* outHitPoint, glm::vec3* outHitNormal, uint64_t* outEntityID)
	{
		KBR_TRACY_FUNCTION();

		if (!origin || !direction || !outHitPoint || !outHitNormal)
			return 0;

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Ref<Scene> currentScene = scene.lock();

		RaycastHit hitInfo;
		if (currentScene->GetPhysicsSystem().Raycast(*origin, *direction, maxDistance, hitInfo))
		{
			*outHitPoint = hitInfo.Point;
			*outHitNormal = hitInfo.Normal;
			*outEntityID = hitInfo.EntityID;
			return 1;
		}
		return 0;
	}

	static int32_t WriteOverlapEntities(const std::vector<OverlapHit>& hits,
	                                    uint64_t* outEntities,
	                                    const int32_t maxEntities,
	                                    const uint64_t ignoreEntityID)
	{
		if (!outEntities || maxEntities <= 0)
			return 0;

		int32_t count = 0;
		for (const OverlapHit& hit : hits)
		{
			if (!hit.EntityID.IsValid())
				continue;
			if (ignoreEntityID != 0 && static_cast<uint64_t>(hit.EntityID) == ignoreEntityID)
				continue;
			if (count >= maxEntities)
				break;

			outEntities[count++] = static_cast<uint64_t>(hit.EntityID);
		}

		return count;
	}

	static int32_t Physics_OverlapBox(const glm::vec3* center,
	                                  const glm::vec3* halfExtents,
	                                  uint64_t* outEntities,
	                                  const int32_t maxEntities)
	{
		KBR_TRACY_FUNCTION();

		if (!center || !halfExtents)
			return 0;

		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return 0;

		const std::vector<OverlapHit> hits = scene->GetPhysicsSystem().OverlapBox(
			*center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), *halfExtents);
		return WriteOverlapEntities(hits, outEntities, maxEntities, 0);
	}

	static int32_t Physics_OverlapBoxForEntity(const uint64_t entityID,
	                                           const glm::vec3* localCenter,
	                                           const glm::vec3* localHalfExtents,
	                                           const uint64_t ignoreEntityID,
	                                           uint64_t* outEntities,
	                                           const int32_t maxEntities)
	{
		KBR_TRACY_FUNCTION();

		if (!localCenter || !localHalfExtents)
			return 0;

		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return 0;

		const Entity entity = scene->TryGetEntityByUUID(UUID(entityID));
		if (!entity || !entity.HasComponent<TransformComponent>())
			return 0;

		const glm::mat4& world = entity.GetComponent<TransformComponent>().WorldTransform;
		const glm::vec3 center = glm::vec3(world * glm::vec4(*localCenter, 1.0f));
		glm::quat rotation = glm::quat_cast(glm::mat3(world));
		const float rotationLength = glm::length(rotation);
		rotation = rotationLength > 1.0e-6f
			? rotation / rotationLength
			: glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		const glm::vec3 scale = {
			glm::length(glm::vec3(world[0])),
			glm::length(glm::vec3(world[1])),
			glm::length(glm::vec3(world[2])),
		};
		const glm::vec3 halfExtents = glm::abs(*localHalfExtents * scale);

		const std::vector<OverlapHit> hits =
			scene->GetPhysicsSystem().OverlapBox(center, rotation, halfExtents);
		return WriteOverlapEntities(hits, outEntities, maxEntities, ignoreEntityID);
	}

	static int32_t Physics_OverlapSphere(const glm::vec3* center,
	                                     const float radius,
	                                     uint64_t* outEntities,
	                                     const int32_t maxEntities)
	{
		KBR_TRACY_FUNCTION();

		if (!center)
			return 0;

		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return 0;

		const std::vector<OverlapHit> hits = scene->GetPhysicsSystem().OverlapSphere(*center, radius);
		return WriteOverlapEntities(hits, outEntities, maxEntities, 0);
	}

	static int32_t Physics_OverlapSphereForEntity(const uint64_t entityID,
	                                              const glm::vec3* localCenter,
	                                              const float radius,
	                                              const uint64_t ignoreEntityID,
	                                              uint64_t* outEntities,
	                                              const int32_t maxEntities)
	{
		KBR_TRACY_FUNCTION();

		if (!localCenter)
			return 0;

		const Ref<Scene> scene = ScriptEngine::GetSceneContext().lock();
		if (!scene)
			return 0;

		const Entity entity = scene->TryGetEntityByUUID(UUID(entityID));
		if (!entity || !entity.HasComponent<TransformComponent>())
			return 0;

		const glm::mat4& world = entity.GetComponent<TransformComponent>().WorldTransform;
		const glm::vec3 center = glm::vec3(world * glm::vec4(*localCenter, 1.0f));
		const glm::vec3 scale = {
			glm::length(glm::vec3(world[0])),
			glm::length(glm::vec3(world[1])),
			glm::length(glm::vec3(world[2])),
		};
		const float scaledRadius = radius * glm::max(scale.x, glm::max(scale.y, scale.z));

		const std::vector<OverlapHit> hits =
			scene->GetPhysicsSystem().OverlapSphere(center, scaledRadius);
		return WriteOverlapEntities(hits, outEntities, maxEntities, ignoreEntityID);
	}

	static uint8_t Input_IsKeyDown(const int key)
	{
		return Input::IsKeyPressed(static_cast<KeyCode>(key)) ? 1 : 0;
	}

	static uint8_t Input_IsMouseButtonDown(const int button)
	{
		return Input::IsMouseButtonPressed(static_cast<MouseButtonCode>(button)) ? 1 : 0;
	}

	// ====================================================================
	// Component Registration
	// ====================================================================

	template<typename Component>
	static void RegisterComponent(const std::string& managedTypeName)
	{
		s_EntityHasComponentFunctions[managedTypeName] = [](const Entity entity) { return entity.HasComponent<Component>(); };
	}

	void ScriptInterface::RegisterFunctions()
	{
		KBR_TRACY_FUNCTION();

		/// Register component types for HasComponent checks
		RegisterComponent<TransformComponent>("Kerberos.Scene.TransformComponent");
		RegisterComponent<TagComponent>("Kerberos.Scene.TagComponent");
		RegisterComponent<StaticMeshComponent>("Kerberos.Scene.StaticMeshComponent");
		RegisterComponent<RigidBody3DComponent>("Kerberos.Scene.RigidBody3DComponent");
		RegisterComponent<BoxCollider3DComponent>("Kerberos.Scene.BoxCollider3DComponent");
		RegisterComponent<SphereCollider3DComponent>("Kerberos.Scene.SphereCollider3DComponent");
		RegisterComponent<CapsuleCollider3DComponent>("Kerberos.Scene.CapsuleCollider3DComponent");
		RegisterComponent<MeshCollider3DComponent>("Kerberos.Scene.MeshCollider3DComponent");
		RegisterComponent<TextComponent>("Kerberos.Scene.TextComponent");
		RegisterComponent<AudioSource2DComponent>("Kerberos.Scene.AudioSource2DComponent");
		RegisterComponent<AudioSource3DComponent>("Kerberos.Scene.AudioSource3DComponent");
		RegisterComponent<AudioListenerComponent>("Kerberos.Scene.AudioListenerComponent");
		RegisterComponent<AnimatorComponent>("Kerberos.Scene.AnimatorComponent");
		RegisterComponent<SocketComponent>("Kerberos.Scene.SocketComponent");
		RegisterComponent<RagdollComponent>("Kerberos.Scene.RagdollComponent");

		/// Build the native callback table and pass it to the managed side
		static NativeCallbackTable callbackTable = {};

		callbackTable.NativeLog = reinterpret_cast<void*>(&NativeLog);
		callbackTable.Entity_HasComponent = reinterpret_cast<void*>(&Entity_HasComponent);
		callbackTable.Entity_AddComponent = reinterpret_cast<void*>(&Entity_AddComponent);
		callbackTable.Entity_FindEntityByName = reinterpret_cast<void*>(&Entity_FindEntityByName);
		callbackTable.Entity_Instantiate = reinterpret_cast<void*>(&Entity_Instantiate);

		callbackTable.TransformComponent_GetTranslation = reinterpret_cast<void*>(&TransformComponent_GetTranslation);
		callbackTable.TransformComponent_SetTranslation = reinterpret_cast<void*>(&TransformComponent_SetTranslation);
		callbackTable.TransformComponent_GetRotation = reinterpret_cast<void*>(&TransformComponent_GetRotation);
		callbackTable.TransformComponent_SetRotation = reinterpret_cast<void*>(&TransformComponent_SetRotation);
		callbackTable.TransformComponent_GetScale = reinterpret_cast<void*>(&TransformComponent_GetScale);
		callbackTable.TransformComponent_SetScale = reinterpret_cast<void*>(&TransformComponent_SetScale);

		callbackTable.StaticMeshComponent_GetMesh = reinterpret_cast<void*>(&StaticMeshComponent_GetMesh);
		callbackTable.StaticMeshComponent_SetMesh = reinterpret_cast<void*>(&StaticMeshComponent_SetMesh);

		callbackTable.Rigidbody3DComponent_GetVelocity = reinterpret_cast<void*>(&Rigidbody3DComponent_GetVelocity);
		callbackTable.Rigidbody3DComponent_SetVelocity = reinterpret_cast<void*>(&Rigidbody3DComponent_SetVelocity);
		callbackTable.Rigidbody3DComponent_ApplyImpulse = reinterpret_cast<void*>(&Rigidbody3DComponent_ApplyImpulse);
		callbackTable.Rigidbody3DComponent_ApplyImpulseAtPoint = reinterpret_cast<void*>(&Rigidbody3DComponent_ApplyImpulseAtPoint);

		callbackTable.TextComponent_SetText = reinterpret_cast<void*>(&TextComponent_SetText);
		callbackTable.TextComponent_GetText = reinterpret_cast<void*>(&TextComponent_GetText);
		callbackTable.TextComponent_SetColor = reinterpret_cast<void*>(&TextComponent_SetColor);
		callbackTable.TextComponent_GetColor = reinterpret_cast<void*>(&TextComponent_GetColor);
		callbackTable.TextComponent_SetFontSize = reinterpret_cast<void*>(&TextComponent_SetFontSize);
		callbackTable.TextComponent_GetFontSize = reinterpret_cast<void*>(&TextComponent_GetFontSize);
		callbackTable.TextComponent_SetFontPath = reinterpret_cast<void*>(&TextComponent_SetFontPath);
		callbackTable.TextComponent_GetFontPath = reinterpret_cast<void*>(&TextComponent_GetFontPath);

		callbackTable.Input_IsKeyDown = reinterpret_cast<void*>(&Input_IsKeyDown);
		callbackTable.Input_IsMouseButtonDown = reinterpret_cast<void*>(&Input_IsMouseButtonDown);

		callbackTable.AudioSource2DComponent_Play = reinterpret_cast<void*>(&AudioSource2DComponent_Play);
		callbackTable.AudioSource2DComponent_Stop = reinterpret_cast<void*>(&AudioSource2DComponent_Stop);
		callbackTable.AudioSource2DComponent_SetVolume = reinterpret_cast<void*>(&AudioSource2DComponent_SetVolume);
		callbackTable.AudioSource2DComponent_GetVolume = reinterpret_cast<void*>(&AudioSource2DComponent_GetVolume);
		callbackTable.AudioSource2DComponent_SetLooping = reinterpret_cast<void*>(&AudioSource2DComponent_SetLooping);
		callbackTable.AudioSource2DComponent_IsLooping = reinterpret_cast<void*>(&AudioSource2DComponent_IsLooping);

		callbackTable.AudioSource3DComponent_Play = reinterpret_cast<void*>(&AudioSource3DComponent_Play);
		callbackTable.AudioSource3DComponent_Stop = reinterpret_cast<void*>(&AudioSource3DComponent_Stop);
		callbackTable.AudioSource3DComponent_SetVolume = reinterpret_cast<void*>(&AudioSource3DComponent_SetVolume);
		callbackTable.AudioSource3DComponent_GetVolume = reinterpret_cast<void*>(&AudioSource3DComponent_GetVolume);
		callbackTable.AudioSource3DComponent_SetLooping = reinterpret_cast<void*>(&AudioSource3DComponent_SetLooping);
		callbackTable.AudioSource3DComponent_IsLooping = reinterpret_cast<void*>(&AudioSource3DComponent_IsLooping);

		callbackTable.Physics_Raycast = reinterpret_cast<void*>(&Physics_Raycast);
		callbackTable.Physics_OverlapBox = reinterpret_cast<void*>(&Physics_OverlapBox);
		callbackTable.Physics_OverlapBoxForEntity = reinterpret_cast<void*>(&Physics_OverlapBoxForEntity);
		callbackTable.Physics_OverlapSphere = reinterpret_cast<void*>(&Physics_OverlapSphere);
		callbackTable.Physics_OverlapSphereForEntity = reinterpret_cast<void*>(&Physics_OverlapSphereForEntity);

		callbackTable.Entity_InstantiatePrefab = reinterpret_cast<void*>(&Entity_InstantiatePrefab);

		callbackTable.AnimatorComponent_Play = reinterpret_cast<void*>(&AnimatorComponent_Play);
		callbackTable.AnimatorComponent_PlayByName = reinterpret_cast<void*>(&AnimatorComponent_PlayByName);
		callbackTable.AnimatorComponent_Stop = reinterpret_cast<void*>(&AnimatorComponent_Stop);
		callbackTable.AnimatorComponent_Pause = reinterpret_cast<void*>(&AnimatorComponent_Pause);
		callbackTable.AnimatorComponent_GetTime = reinterpret_cast<void*>(&AnimatorComponent_GetTime);
		callbackTable.AnimatorComponent_SetTime = reinterpret_cast<void*>(&AnimatorComponent_SetTime);
		callbackTable.AnimatorComponent_GetSpeed = reinterpret_cast<void*>(&AnimatorComponent_GetSpeed);
		callbackTable.AnimatorComponent_SetSpeed = reinterpret_cast<void*>(&AnimatorComponent_SetSpeed);
		callbackTable.AnimatorComponent_IsPlaying = reinterpret_cast<void*>(&AnimatorComponent_IsPlaying);
		callbackTable.AnimatorComponent_SetPlaying = reinterpret_cast<void*>(&AnimatorComponent_SetPlaying);
		callbackTable.AnimatorComponent_IsLooping = reinterpret_cast<void*>(&AnimatorComponent_IsLooping);
		callbackTable.AnimatorComponent_SetLooping = reinterpret_cast<void*>(&AnimatorComponent_SetLooping);
		callbackTable.AnimatorComponent_GetPlayOnStart = reinterpret_cast<void*>(&AnimatorComponent_GetPlayOnStart);
		callbackTable.AnimatorComponent_SetPlayOnStart = reinterpret_cast<void*>(&AnimatorComponent_SetPlayOnStart);
		callbackTable.AnimatorComponent_GetActiveClip = reinterpret_cast<void*>(&AnimatorComponent_GetActiveClip);
		callbackTable.AnimatorComponent_SetActiveClip = reinterpret_cast<void*>(&AnimatorComponent_SetActiveClip);
		callbackTable.AnimatorComponent_GetClipCount = reinterpret_cast<void*>(&AnimatorComponent_GetClipCount);
		callbackTable.AnimatorComponent_GetClipDuration = reinterpret_cast<void*>(&AnimatorComponent_GetClipDuration);
		callbackTable.AnimatorComponent_CrossFade = reinterpret_cast<void*>(&AnimatorComponent_CrossFade);
		callbackTable.AnimatorComponent_CrossFadeByName = reinterpret_cast<void*>(&AnimatorComponent_CrossFadeByName);

		callbackTable.Socket_Attach = reinterpret_cast<void*>(&Socket_Attach);
		callbackTable.Socket_Detach = reinterpret_cast<void*>(&Socket_Detach);

		callbackTable.RagdollComponent_EnableRagdoll = reinterpret_cast<void*>(&RagdollComponent_EnableRagdoll);
		callbackTable.RagdollComponent_DisableRagdoll = reinterpret_cast<void*>(&RagdollComponent_DisableRagdoll);
		callbackTable.RagdollComponent_IsRagdoll = reinterpret_cast<void*>(&RagdollComponent_IsRagdoll);
		callbackTable.RagdollComponent_GetState = reinterpret_cast<void*>(&RagdollComponent_GetState);
		callbackTable.RagdollComponent_GetWeight = reinterpret_cast<void*>(&RagdollComponent_GetWeight);
		callbackTable.RagdollComponent_GetBlendDuration = reinterpret_cast<void*>(&RagdollComponent_GetBlendDuration);
		callbackTable.RagdollComponent_SetBlendDuration = reinterpret_cast<void*>(&RagdollComponent_SetBlendDuration);
		callbackTable.RagdollComponent_GetStartInRagdoll = reinterpret_cast<void*>(&RagdollComponent_GetStartInRagdoll);
		callbackTable.RagdollComponent_SetStartInRagdoll = reinterpret_cast<void*>(&RagdollComponent_SetStartInRagdoll);
		callbackTable.RagdollComponent_GetJointRadiusScale = reinterpret_cast<void*>(&RagdollComponent_GetJointRadiusScale);
		callbackTable.RagdollComponent_SetJointRadiusScale = reinterpret_cast<void*>(&RagdollComponent_SetJointRadiusScale);

		callbackTable.Rigidbody3DComponent_GetBodyType = reinterpret_cast<void*>(&Rigidbody3DComponent_GetBodyType);
		callbackTable.Rigidbody3DComponent_SetBodyType = reinterpret_cast<void*>(&Rigidbody3DComponent_SetBodyType);
		callbackTable.Rigidbody3DComponent_GetMass = reinterpret_cast<void*>(&Rigidbody3DComponent_GetMass);
		callbackTable.Rigidbody3DComponent_SetMass = reinterpret_cast<void*>(&Rigidbody3DComponent_SetMass);
		callbackTable.Rigidbody3DComponent_GetAngularVelocity = reinterpret_cast<void*>(&Rigidbody3DComponent_GetAngularVelocity);
		callbackTable.Rigidbody3DComponent_SetAngularVelocity = reinterpret_cast<void*>(&Rigidbody3DComponent_SetAngularVelocity);
		callbackTable.Rigidbody3DComponent_GetUseGravity = reinterpret_cast<void*>(&Rigidbody3DComponent_GetUseGravity);
		callbackTable.Rigidbody3DComponent_SetUseGravity = reinterpret_cast<void*>(&Rigidbody3DComponent_SetUseGravity);
		callbackTable.Rigidbody3DComponent_GetFriction = reinterpret_cast<void*>(&Rigidbody3DComponent_GetFriction);
		callbackTable.Rigidbody3DComponent_SetFriction = reinterpret_cast<void*>(&Rigidbody3DComponent_SetFriction);
		callbackTable.Rigidbody3DComponent_GetRestitution = reinterpret_cast<void*>(&Rigidbody3DComponent_GetRestitution);
		callbackTable.Rigidbody3DComponent_SetRestitution = reinterpret_cast<void*>(&Rigidbody3DComponent_SetRestitution);

		callbackTable.BoxCollider3DComponent_GetSize = reinterpret_cast<void*>(&BoxCollider3DComponent_GetSize);
		callbackTable.BoxCollider3DComponent_SetSize = reinterpret_cast<void*>(&BoxCollider3DComponent_SetSize);
		callbackTable.BoxCollider3DComponent_GetOffset = reinterpret_cast<void*>(&BoxCollider3DComponent_GetOffset);
		callbackTable.BoxCollider3DComponent_SetOffset = reinterpret_cast<void*>(&BoxCollider3DComponent_SetOffset);
		callbackTable.BoxCollider3DComponent_GetIsTrigger = reinterpret_cast<void*>(&BoxCollider3DComponent_GetIsTrigger);
		callbackTable.BoxCollider3DComponent_SetIsTrigger = reinterpret_cast<void*>(&BoxCollider3DComponent_SetIsTrigger);
		callbackTable.BoxCollider3DComponent_GetEnabled = reinterpret_cast<void*>(&BoxCollider3DComponent_GetEnabled);
		callbackTable.BoxCollider3DComponent_SetEnabled = reinterpret_cast<void*>(&BoxCollider3DComponent_SetEnabled);

		callbackTable.SphereCollider3DComponent_GetRadius = reinterpret_cast<void*>(&SphereCollider3DComponent_GetRadius);
		callbackTable.SphereCollider3DComponent_SetRadius = reinterpret_cast<void*>(&SphereCollider3DComponent_SetRadius);
		callbackTable.SphereCollider3DComponent_GetOffset = reinterpret_cast<void*>(&SphereCollider3DComponent_GetOffset);
		callbackTable.SphereCollider3DComponent_SetOffset = reinterpret_cast<void*>(&SphereCollider3DComponent_SetOffset);
		callbackTable.SphereCollider3DComponent_GetIsTrigger = reinterpret_cast<void*>(&SphereCollider3DComponent_GetIsTrigger);
		callbackTable.SphereCollider3DComponent_SetIsTrigger = reinterpret_cast<void*>(&SphereCollider3DComponent_SetIsTrigger);
		callbackTable.SphereCollider3DComponent_GetEnabled = reinterpret_cast<void*>(&SphereCollider3DComponent_GetEnabled);
		callbackTable.SphereCollider3DComponent_SetEnabled = reinterpret_cast<void*>(&SphereCollider3DComponent_SetEnabled);

		callbackTable.CapsuleCollider3DComponent_GetRadius = reinterpret_cast<void*>(&CapsuleCollider3DComponent_GetRadius);
		callbackTable.CapsuleCollider3DComponent_SetRadius = reinterpret_cast<void*>(&CapsuleCollider3DComponent_SetRadius);
		callbackTable.CapsuleCollider3DComponent_GetHeight = reinterpret_cast<void*>(&CapsuleCollider3DComponent_GetHeight);
		callbackTable.CapsuleCollider3DComponent_SetHeight = reinterpret_cast<void*>(&CapsuleCollider3DComponent_SetHeight);
		callbackTable.CapsuleCollider3DComponent_GetOffset = reinterpret_cast<void*>(&CapsuleCollider3DComponent_GetOffset);
		callbackTable.CapsuleCollider3DComponent_SetOffset = reinterpret_cast<void*>(&CapsuleCollider3DComponent_SetOffset);
		callbackTable.CapsuleCollider3DComponent_GetIsTrigger = reinterpret_cast<void*>(&CapsuleCollider3DComponent_GetIsTrigger);
		callbackTable.CapsuleCollider3DComponent_SetIsTrigger = reinterpret_cast<void*>(&CapsuleCollider3DComponent_SetIsTrigger);
		callbackTable.CapsuleCollider3DComponent_GetEnabled = reinterpret_cast<void*>(&CapsuleCollider3DComponent_GetEnabled);
		callbackTable.CapsuleCollider3DComponent_SetEnabled = reinterpret_cast<void*>(&CapsuleCollider3DComponent_SetEnabled);

		callbackTable.MeshCollider3DComponent_GetMesh = reinterpret_cast<void*>(&MeshCollider3DComponent_GetMesh);
		callbackTable.MeshCollider3DComponent_SetMesh = reinterpret_cast<void*>(&MeshCollider3DComponent_SetMesh);
		callbackTable.MeshCollider3DComponent_GetOffset = reinterpret_cast<void*>(&MeshCollider3DComponent_GetOffset);
		callbackTable.MeshCollider3DComponent_SetOffset = reinterpret_cast<void*>(&MeshCollider3DComponent_SetOffset);
		callbackTable.MeshCollider3DComponent_GetIsTrigger = reinterpret_cast<void*>(&MeshCollider3DComponent_GetIsTrigger);
		callbackTable.MeshCollider3DComponent_SetIsTrigger = reinterpret_cast<void*>(&MeshCollider3DComponent_SetIsTrigger);
		callbackTable.MeshCollider3DComponent_GetEnabled = reinterpret_cast<void*>(&MeshCollider3DComponent_GetEnabled);
		callbackTable.MeshCollider3DComponent_SetEnabled = reinterpret_cast<void*>(&MeshCollider3DComponent_SetEnabled);

		/// Pass the callback table to the managed side
		ScriptEngine::SetManagedNativeCallbacks(&callbackTable);
	}
}