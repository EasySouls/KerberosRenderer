#include "kbrpch.hpp"

#include "ScriptInterface.hpp"

#include "Logging/Log.hpp"
#include "Input/InputSystem.hpp"
#include "Input/KeyCodes.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptUtils.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Components/AudioComponents.hpp"

#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>

#include <memory>


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
	};

	/// Component type name to HasComponent check mapping
	static std::unordered_map<std::string, std::function<bool(Entity)>> s_EntityHasComponentFunctions{};

	// ====================================================================
	// Native callback implementations (called from C# via function pointers)
	// ====================================================================

	static void NativeLog(const char* message)
	{
		if (message)
			KBR_CORE_INFO("C# Log: {0}", message);
	}

	static uint8_t Entity_HasComponent(const uint64_t entityID, const char* componentTypeName)
	{
		if (!componentTypeName)
			return 0;

		if (const std::shared_ptr<Scene> scene = ScriptEngine::GetSceneContext().lock())
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

		KBR_CORE_ASSERT(false, "Unknown component type: {0}", typeName);
		return nullptr;
	}

	static void Entity_AddComponent(const uint64_t entityID, const char* componentTypeName)
	{
		if (!componentTypeName)
			return;

		if (const std::shared_ptr<Scene> scene = ScriptEngine::GetSceneContext().lock())
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
		if (!name)
			return UUID::Invalid();

		const std::string nameStr(name);

		if (const std::shared_ptr<Scene> scene = ScriptEngine::GetSceneContext().lock())
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
		const std::string nameStr = name ? std::string(name) : "Entity";
		if (const std::shared_ptr<Scene> scene = ScriptEngine::GetSceneContext().lock())
		{
			const Entity entity = scene->CreateEntity(nameStr);
			return entity.GetUUID();
		}
		return UUID::Invalid();
	}

	static void TransformComponent_GetTranslation(const uint64_t entityID, glm::vec3* outTranslation)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outTranslation)
		{
			const glm::vec3 translation = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TransformComponent>().Translation;
			*outTranslation = translation;
		}
	}

	static void TransformComponent_SetTranslation(const uint64_t entityID, const glm::vec3* translation)
	{
		if (translation)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			const auto& ownedScene = scene.lock();
			if (!ownedScene)
			{
				KBR_CORE_ERROR("Failed to set translation for entity ID: {}. Scene context is not valid.", entityID);
				return;
			}

			const Entity entity = ownedScene->GetEntityByUUID(UUID(entityID));
			glm::vec3& currentTranslation = entity.GetComponent<TransformComponent>().Translation;
			currentTranslation = *translation;
			ownedScene->CalculateEntityTransform(entity);
		}
	}

	static void TransformComponent_GetRotation(const uint64_t entityID, glm::vec3* outRotation)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outRotation)
		{
			const glm::vec3 rotation = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TransformComponent>().Rotation;
			*outRotation = rotation;
		}
	}

	static void TransformComponent_SetRotation(const uint64_t entityID, const glm::vec3* rotation)
	{
		if (rotation)
		{
			const WeakRef<Scene>& scene = ScriptEngine::GetSceneContext();
			const auto& ownedScene = scene.lock();
			if (!ownedScene)
			{
				KBR_CORE_ERROR("Failed to set rotation for entity ID: {}. Scene context is not valid.", entityID);
				return;
			}

			const Entity entity = ownedScene->GetEntityByUUID(UUID(entityID));
			glm::vec3& currentRotation = entity.GetComponent<TransformComponent>().Rotation;
			currentRotation = *rotation;
			ownedScene->CalculateEntityTransform(entity);
		}
	}

	static void TransformComponent_GetScale(const uint64_t entityID, glm::vec3* outScale)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outScale)
		{
			const glm::vec3 scale = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TransformComponent>().Scale;
			*outScale = scale;
		}
	}

	static void TransformComponent_SetScale(const uint64_t entityID, const glm::vec3* scale)
	{
		if (scale)
		{
			const WeakRef<Scene>& scene = ScriptEngine::GetSceneContext();
			const auto& ownedScene = scene.lock();
			if (!ownedScene)
			{
				KBR_CORE_ERROR("Failed to set scale for entity ID: {}. Scene context is not valid.", entityID);
				return;
			}

			const Entity entity = ownedScene->GetEntityByUUID(UUID(entityID));
			glm::vec3& currentScale = entity.GetComponent<TransformComponent>().Scale;
			currentScale = *scale;
			ownedScene->CalculateEntityTransform(entity);
		}
	}

	static void StaticMeshComponent_GetMesh(const uint64_t entityID, uint64_t* outMesh)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Ref<Scene> currentScene = scene.lock();
		const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

		KBR_CORE_ASSERT(entity.HasComponent<StaticMeshComponent>(), "Entity doesn't have a StaticMeshComponent.");

		const StaticMeshComponent& staticMeshComponent = entity.GetComponent<StaticMeshComponent>();
		*outMesh = staticMeshComponent.StaticMesh->GetHandle();
	}

	static void StaticMeshComponent_SetMesh(const uint64_t entityID, const uint64_t meshRef)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Ref<Scene> currentScene = scene.lock();
		const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

		KBR_CORE_ASSERT(entity.HasComponent<StaticMeshComponent>(), "Entity doesn't have a StaticMeshComponent.");
		// TODO: when using asset handles for assets, simply set it, maybe check if correct

		StaticMeshComponent& staticMeshComponent = entity.GetComponent<StaticMeshComponent>();
		const Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(AssetHandle(meshRef));
		if (!mesh)
		{
			KBR_CORE_WARN("Failed to set static mesh for entity ID: {}, mesh asset ID: {}", entityID, meshRef);
		}
		staticMeshComponent.StaticMesh = mesh;
	}

	static void Rigidbody3DComponent_GetVelocity(const uint64_t entityID, glm::vec3* outVelocity)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		if (outVelocity)
		{
			const glm::vec3 velocity = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<RigidBody3DComponent>().Velocity;
			*outVelocity = velocity;
		}
	}

	static void Rigidbody3DComponent_SetVelocity(const uint64_t entityID, const glm::vec3* velocity)
	{
		if (velocity)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			glm::vec3& currentVelocity = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<RigidBody3DComponent>().Velocity;
			currentVelocity = *velocity;
		}
	}

	static void Rigidbody3DComponent_ApplyImpulse(const uint64_t entityID, const glm::vec3* force)
	{
		if (force)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			const Ref<Scene> currentScene = scene.lock();
			const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

			KBR_CORE_ASSERT(entity.HasComponent<RigidBody3DComponent>(), "Entity doesn't have a Rigidbody3DComponent.");

			const RigidBody3DComponent& rb3d = entity.GetComponent<RigidBody3DComponent>();
			KBR_CORE_ASSERT(rb3d.RuntimeBody, "Rigidbody3DComponent doesn't have a runtime body.");

			const JPH::Body* body = static_cast<JPH::Body*>(rb3d.RuntimeBody);
			const JPH::BodyID& bodyId = body->GetID();
			const uint32_t bodyIdValue = bodyId.GetIndexAndSequenceNumber();

			const PhysicsSystem& physicsSystem = currentScene->GetPhysicsSystem();
			physicsSystem.AddImpulse(bodyIdValue, *force);
		}
	}

	static void Rigidbody3DComponent_ApplyImpulseAtPoint(const uint64_t entityID, const glm::vec3* force, const glm::vec3* inPoint)
	{
		if (force)
		{
			const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
			const Ref<Scene> currentScene = scene.lock();
			const Entity entity = currentScene->GetEntityByUUID(UUID(entityID));

			KBR_CORE_ASSERT(entity.HasComponent<RigidBody3DComponent>(), "Entity doesn't have a Rigidbody3DComponent.");

			const RigidBody3DComponent& rb3d = entity.GetComponent<RigidBody3DComponent>();
			KBR_CORE_ASSERT(rb3d.RuntimeBody, "Rigidbody3DComponent doesn't have a runtime body.");

			const JPH::Body* body = static_cast<JPH::Body*>(rb3d.RuntimeBody);
			const JPH::BodyID& bodyId = body->GetID();
			const uint32_t bodyIdValue = bodyId.GetIndexAndSequenceNumber();

			const PhysicsSystem& physicsSystem = currentScene->GetPhysicsSystem();
			physicsSystem.AddImpulse(bodyIdValue, *force, *inPoint);
		}
	}

	/// String return buffer (managed side will read from this pointer)
	static std::string s_StringReturnBuffer;

	static const char* TextComponent_GetText(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));
		const TextComponent& textComponent = entity.GetComponent<TextComponent>();
		s_StringReturnBuffer = textComponent.Text;
		return s_StringReturnBuffer.c_str();
	}

	static void TextComponent_SetText(const uint64_t entityID, const char* text)
	{
		if (!text)
			return;

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		TextComponent& textComponent = entity.GetComponent<TextComponent>();
		textComponent.Text = std::string(text);
	}

	static void TextComponent_GetColor(const uint64_t entityID, glm::vec4* outColor)
	{
		if (!outColor)
			return;

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const glm::vec4 color = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().Color;
		*outColor = color;
	}

	static void TextComponent_SetColor(const uint64_t entityID, const glm::vec4* color)
	{
		if (!color)
			return;

		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		glm::vec4& currentColor = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().Color;
		currentColor = *color;
	}

	static float TextComponent_GetFontSize(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const float fontSize = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().FontSize;
		return fontSize;
	}

	static void TextComponent_SetFontSize(const uint64_t entityID, const float fontSize)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		float& currentFontSize = scene.lock()->GetEntityByUUID(UUID(entityID)).GetComponent<TextComponent>().FontSize;
		currentFontSize = fontSize;
	}

	static const char* TextComponent_GetFontPath(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const TextComponent& textComponent = entity.GetComponent<TextComponent>();
		s_StringReturnBuffer = textComponent.Font->GetFilepath().string();
		return s_StringReturnBuffer.c_str();
	}

	static void TextComponent_SetFontPath(const uint64_t entityID, const char* fontPath)
	{
		if (!fontPath)
			return;

		throw std::runtime_error("TextComponent_SetFontPath is not implemented yet");
	}

	static void AudioSource2DComponent_Play(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Play();
	}

	static void AudioSource2DComponent_Stop(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Stop();
	}

	static float AudioSource2DComponent_GetVolume(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		return audioComponent.Volume;
	}

	static void AudioSource2DComponent_SetVolume(const uint64_t entityID, const float volume)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		audioComponent.Volume = volume;
		audioComponent.SoundAsset->SetVolume(volume);
	}

	static void AudioSource2DComponent_SetLooping(const uint64_t entityID, const uint8_t loop)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		audioComponent.Loop = loop != 0;
	}

	static uint8_t AudioSource2DComponent_IsLooping(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource2DComponent& audioComponent = entity.GetComponent<AudioSource2DComponent>();
		return audioComponent.Loop ? 1 : 0;
	}

	static void AudioSource3DComponent_Play(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Play();
	}

	static void AudioSource3DComponent_Stop(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		if (audioComponent.SoundAsset)
			audioComponent.SoundAsset->Stop();
	}

	static float AudioSource3DComponent_GetVolume(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		return audioComponent.Volume;
	}

	static void AudioSource3DComponent_SetVolume(const uint64_t entityID, const float volume)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		audioComponent.Volume = volume;
		audioComponent.SoundAsset->SetVolume(volume);
	}

	static void AudioSource3DComponent_SetLooping(const uint64_t entityID, const uint8_t loop)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		audioComponent.Loop = loop != 0;
	}

	static uint8_t AudioSource3DComponent_IsLooping(const uint64_t entityID)
	{
		const std::weak_ptr<Scene>& scene = ScriptEngine::GetSceneContext();
		const Entity entity = scene.lock()->GetEntityByUUID(UUID(entityID));

		const AudioSource3DComponent& audioComponent = entity.GetComponent<AudioSource3DComponent>();
		return audioComponent.Loop ? 1 : 0;
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
		/// Register component types for HasComponent checks
		RegisterComponent<TransformComponent>("Kerberos.Source.Kerberos.Scene.TransformComponent");
		RegisterComponent<TagComponent>("Kerberos.Source.Kerberos.Scene.TagComponent");
		RegisterComponent<StaticMeshComponent>("Kerberos.Source.Kerberos.Scene.StaticMeshComponent");
		RegisterComponent<RigidBody3DComponent>("Kerberos.Source.Kerberos.Scene.RigidBody3DComponent");
		RegisterComponent<TextComponent>("Kerberos.Source.Kerberos.Scene.TextComponent");
		RegisterComponent<AudioSource2DComponent>("Kerberos.Source.Kerberos.Scene.AudioSource2DComponent");
		RegisterComponent<AudioSource3DComponent>("Kerberos.Source.Kerberos.Scene.AudioSource3DComponent");
		RegisterComponent<AudioListenerComponent>("Kerberos.Source.Kerberos.Scene.AudioListenerComponent");

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

		/// Pass the callback table to the managed side
		ScriptEngine::SetManagedNativeCallbacks(&callbackTable);
	}
}
