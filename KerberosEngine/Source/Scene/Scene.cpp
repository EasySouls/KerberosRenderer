#include "kbrpch.hpp"

#include "Scene.hpp"
#include "Entity.hpp"
#include "Components.hpp"
#include "Components/PhysicsComponents.hpp"
#include "Components/AudioComponents.hpp"
#include "Components/ParticleComponents.hpp"
#include "Components/AnimationComponents.hpp"
#include "Application.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/Prefab.hpp"
#include "Renderer/Renderer.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Physics/Jolt/Utils.hpp"

#include <glm/gtx/matrix_decompose.hpp>

#include <utility>


#define USE_MAP_FOR_UUID 1

namespace Kerberos
{
	Scene::Scene()
		: m_PhysicsSystem(new JoltPhysicsSystem())
	{
		m_Registry = entt::basic_registry();
	}

	Scene::~Scene()
	{
		/// Destroy all entities in the scene
		m_Registry.clear<entt::entity>();
	}

	void Scene::OnRuntimeStart()
	{
		KBR_PROFILE_FUNCTION();

		m_PhysicsSystem->Initialize(shared_from_this());
		m_PhysicsSystem->Update(0.0f); // Sync physics bodies with transforms before scripts run for the first time

		ScriptEngine::OnRuntimeStart(shared_from_this());

		/// Instantiate all scripts
		m_Registry.view<ScriptComponent>().each([this](auto enttId, ScriptComponent& script) {
			const Entity entity{ enttId, this };
			ScriptEngine::OnCreateEntity(entity);
		});
	}

	void Scene::OnRuntimeStop() const
	{
		m_PhysicsSystem->Cleanup();

		ScriptEngine::OnRuntimeStop();
	}

	void Scene::OnSimulationStart()
	{
		m_PhysicsSystem->Initialize(shared_from_this());
	}

	void Scene::OnSimulationStop() const
	{
		m_PhysicsSystem->Cleanup();
	}

	void Scene::SetScenePaused(const bool isPaused)
	{
		m_IsScenePaused = isPaused;
	}

	void Scene::OnUpdateEditor(const float ts, const Camera& camera)
	{
		Render3DEditor(camera, ts);
	}

	void Scene::OnUpdateSimulation(const float ts, const Camera& camera)
	{
		/// If the scene is paused, do not update the physics system, but still render the scene
		if (!m_IsScenePaused)
		{
			m_PhysicsSystem->Update(ts);
		}

		Render3DEditor(camera, ts);
	}

	void Scene::OnUpdateRuntime(const float ts, const Camera& camera)
	{
		KBR_PROFILE_FUNCTION();

		if (!m_IsScenePaused)
		{
			UpdateScripts(ts);

			m_PhysicsSystem->Update(ts);

			Application::Get().GetAudioManager()->Update();

			const std::vector<CollisionEvent> collisionEvents = m_PhysicsSystem->GetCollisionEvents();
			for (const CollisionEvent& event : collisionEvents)
			{
				const Entity entityA = GetEntityByUUID(event.EntityA);
				const Entity entityB = GetEntityByUUID(event.EntityB);

				ScriptEngine::OnCollision(entityA, event);
				ScriptEngine::OnCollision(entityB, event);
			}
		}

		/// Render the scene

		const SceneCamera* mainCamera = nullptr;
		glm::mat4 mainCameraTransform;

		{
			const auto view = m_Registry.view<CameraComponent, TransformComponent>();
			for (const auto entity : view)
			{
				auto [cameraComp, transformComp] = view.get<CameraComponent, TransformComponent>(entity);
				if (cameraComp.IsPrimary)
				{
					mainCamera = &cameraComp.Camera;
					mainCameraTransform = transformComp.GetTransform();
					break;
				}
			}
		}

		//if (mainCamera)
		//{
			if (m_Is3D)
			{
				//Render3DRuntime(mainCamera, mainCameraTransform);
				// TODO: Implement rendering with CameraComponent
				Render3DRuntime(camera, ts);
			}
			else
			{
				Render2DRuntime(mainCamera, mainCameraTransform, ts);
			}
		//}
	}

	Entity Scene::CreateEntity(const std::string& name, const UUID parentId)
	{
		const auto enttId = m_Registry.create();
		Entity entity = { enttId, this };
		m_RootEntities.push_back(enttId);

		entity.AddComponent<TransformComponent>();
		const auto& idComp = entity.AddComponent<IDComponent>();

		auto& hierarchy = entity.AddComponent<HierarchyComponent>();
		if (parentId.IsValid())
		{
			hierarchy.Parent = parentId;
			const Entity parent = GetEntityByUUID(parentId);
			parent.GetComponent<HierarchyComponent>().Children.push_back(entity.GetUUID());
		}

		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

#if USE_MAP_FOR_UUID
		m_UUIDToEntityMap[idComp.ID] = entity;
#endif

		return entity;
	}

	Entity Scene::CreateEntityWithUUID(const std::string& name, const uint64_t uuid)
	{
		const auto enttId = m_Registry.create();
		Entity entity = { enttId, this };
		m_RootEntities.push_back(enttId);

		entity.AddComponent<TransformComponent>();
		entity.AddComponent<HierarchyComponent>();

		auto& idComp = entity.AddComponent<IDComponent>();
		idComp.ID = UUID(uuid);

		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

#if USE_MAP_FOR_UUID
		m_UUIDToEntityMap[idComp.ID] = entity;
#endif

		return entity;
	}

Entity Scene::InstantiatePrefab(const AssetHandle prefabHandle, const std::string& rootName)
{
	KBR_PROFILE_FUNCTION();

	if (!prefabHandle.IsValid())
	{
		KBR_CORE_ERROR("Cannot instantiate prefab with invalid handle.");
		return {};
	}

	if (AssetManager::GetAssetType(prefabHandle) != AssetType::Prefab)
	{
		KBR_CORE_ERROR("Asset {} is not a prefab asset.", prefabHandle);
		return {};
	}

	const Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(prefabHandle);
	if (!prefab)
	{
		KBR_CORE_ERROR("Failed to load prefab asset: {}", prefabHandle);
		return {};
	}

	std::unordered_map<PrefabLocalIndex, Entity> entityMapping;
	std::unordered_map<UUID, PrefabLocalIndex> legacyIndices;

	const std::string resolvedRootName = rootName.empty() ? prefab->GetName() : rootName;

	for (PrefabLocalIndex i = 0; i < prefab->Entities.size(); ++i)
	{
		const auto& prefabEntity = prefab->Entities[i];
		const auto local = prefabEntity.LocalIndex == InvalidPrefabLocalIndex ? i : prefabEntity.LocalIndex;
		if (prefabEntity.ID.IsValid()) legacyIndices[prefabEntity.ID] = local;
		Entity newEntity = CreateEntity(prefabEntity.Name.empty() ? prefabEntity.Tag : prefabEntity.Name);
		entityMapping[local] = newEntity;
		auto& transform = newEntity.GetComponent<TransformComponent>();
		transform.Translation = prefabEntity.Transform.Translation;
		transform.Rotation = prefabEntity.Transform.Rotation;
		transform.Scale = prefabEntity.Transform.Scale;
		transform.WorldTransform = glm::mat4(1.0f);
		if (prefabEntity.SkeletalMesh) {
			auto& c = newEntity.AddComponent<SkeletalMeshComponent>();
			c.MeshAsset = prefabEntity.SkeletalMesh->MeshAsset; c.SkeletonAsset = prefabEntity.SkeletalMesh->SkeletonAsset;
			c.MaterialAsset = prefabEntity.SkeletalMesh->MaterialAsset; c.Visible = prefabEntity.SkeletalMesh->Visible; c.CastShadows = prefabEntity.SkeletalMesh->CastShadows;
		}
		if (prefabEntity.Animation) {
			auto& c = newEntity.AddComponent<AnimationComponent>();
			c.AnimationAsset = prefabEntity.Animation->AnimationAsset; c.PlaybackSpeed = prefabEntity.Animation->PlaybackSpeed;
			c.IsLooping = prefabEntity.Animation->Loop; c.IsPlaying = prefabEntity.Animation->AutoPlay; c.CurrentTime = 0.0f;
		}
		if (prefabEntity.Skin) {
			auto& c = newEntity.AddComponent<SkinComponent>();
			c.SkeletonAsset = prefabEntity.Skin->SkeletonAsset; c.JointMatrices.clear();
		}
		if (prefabEntity.RigidBody) {
			auto& c = newEntity.AddComponent<RigidBody3DComponent>(*prefabEntity.RigidBody);
			c.RuntimeBody = nullptr; c.IsDirty = true;
		}
		if (prefabEntity.BoxCollider) {
			auto& c = newEntity.AddComponent<BoxCollider3DComponent>(*prefabEntity.BoxCollider);
			c.RuntimeCollider = nullptr;
		}
		if (prefabEntity.SphereCollider) {
			auto& c = newEntity.AddComponent<SphereCollider3DComponent>(*prefabEntity.SphereCollider);
			c.RuntimeCollider = nullptr;
		}
		if (prefabEntity.CapsuleCollider) {
			auto& c = newEntity.AddComponent<CapsuleCollider3DComponent>(*prefabEntity.CapsuleCollider);
			c.RuntimeCollider = nullptr;
		}
	}

	for (size_t i = 0; i < prefab->Entities.size(); ++i)
	{
		const auto& prefabEntity = prefab->Entities[i];
		const auto local = prefabEntity.LocalIndex == InvalidPrefabLocalIndex ? static_cast<PrefabLocalIndex>(i) : prefabEntity.LocalIndex;
		Entity child = entityMapping[local];
		auto parentLocal = prefabEntity.ParentLocalIndex;
		if (parentLocal == InvalidPrefabLocalIndex && prefabEntity.Parent.IsValid() && legacyIndices.contains(prefabEntity.Parent))
			parentLocal = legacyIndices[prefabEntity.Parent];
		if (parentLocal != InvalidPrefabLocalIndex && entityMapping.contains(parentLocal))
			SetParent(child, entityMapping[parentLocal], false);
		if (child.HasComponent<SkinComponent>())
		{
			for (auto joint : prefabEntity.Skin->JointEntityIndices)
				if (entityMapping.contains(joint)) child.GetComponent<SkinComponent>().JointEntities.push_back(entityMapping[joint].GetUUID());
			for (const auto& jointId : prefabEntity.Skin->JointEntityIDs)
				if (legacyIndices.contains(jointId)) child.GetComponent<SkinComponent>().JointEntities.push_back(entityMapping[legacyIndices[jointId]].GetUUID());
		}
		if (child.HasComponent<RigidBody3DComponent>()) child.GetComponent<RigidBody3DComponent>().RuntimeBody = nullptr;
	}

	PrefabLocalIndex rootLocal = prefab->RootLocalIndex;
	if (rootLocal == InvalidPrefabLocalIndex && legacyIndices.contains(prefab->RootEntityID)) rootLocal = legacyIndices[prefab->RootEntityID];
	Entity instanceRoot = entityMapping.contains(rootLocal) ? entityMapping[rootLocal] : Entity{};
	if (instanceRoot)
	{
		// Rename the root to match the resolved name
		if (!resolvedRootName.empty())
			instanceRoot.GetComponent<TagComponent>().Tag = resolvedRootName;

		// Attach PrefabInstanceComponent to mark this as a prefab instance
		instanceRoot.AddComponent<PrefabInstanceComponent>(PrefabInstanceComponent{ prefabHandle });
	}

	return instanceRoot;
}

	void Scene::DestroyEntity(const Entity entity)
	{
		const entt::entity enttId = static_cast<entt::entity>(entity);
		if (const auto it = std::ranges::find(m_RootEntities, enttId); it != m_RootEntities.end())
		{
			m_RootEntities.erase(it);
		}

		/// Destroy all children entities
		const auto children = GetChildren(entity);
		for (const auto& child : children)
		{
			DestroyEntity(child);
		}

		// If the entity has a parent, remove the entity from its parent's list of children
		const UUID parentUUID = entity.GetComponent<HierarchyComponent>().Parent;
		if (parentUUID.IsValid())
		{
			const Entity parent = GetEntityByUUID(parentUUID);
			auto& parentHierarchy = parent.GetComponent<HierarchyComponent>();
			parentHierarchy.Children.erase(std::ranges::remove(parentHierarchy.Children, entity.GetUUID()).begin(), parentHierarchy.Children.end());
		}

#if USE_MAP_FOR_UUID
		// Remove the entity from the UUID map before destroying it
		const UUID entityUUID = entity.GetComponent<IDComponent>().ID;
		m_UUIDToEntityMap.erase(entityUUID);
#endif

		m_Registry.destroy(enttId);
	}

	Entity Scene::DuplicateEntity(const Entity entity, const bool duplicateChildren)
	{
		KBR_PROFILE_FUNCTION();

		const std::string name = entity.GetComponent<TagComponent>().Tag;
		const std::string newName = name + " Copy";

		Entity newEntity = CreateEntity(newName);

		newEntity.GetComponent<TransformComponent>() = entity.GetComponent<TransformComponent>();

		if (entity.HasComponent<SpriteRendererComponent>())
		{
			newEntity.AddComponent<SpriteRendererComponent>(entity.GetComponent<SpriteRendererComponent>());
		}
		if (entity.HasComponent<CameraComponent>())
		{
			newEntity.AddComponent<CameraComponent>(entity.GetComponent<CameraComponent>());
			auto& camera = newEntity.GetComponent<CameraComponent>();
			camera.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
		}
		if (entity.HasComponent<ScriptComponent>())
		{
			newEntity.AddComponent<ScriptComponent>(entity.GetComponent<ScriptComponent>());

			ScriptEngine::CopyScriptFieldInitializers(entity, newEntity);
		}
		if (entity.HasComponent<NativeScriptComponent>())
		{
			/*auto& script = entity.GetComponent<NativeScriptComponent>();
			/// Instantiate the script instance, so that script.Instance is not nullptr,
			/// when calling script.Instantiate() in the new entity.
			script.Instantiate();

			auto& newScriptComp = newEntity.AddComponent<NativeScriptComponent>(script);
			newScriptComp.Instantiate = [&]() { newScriptComp.Instance = script.Instance; };
			newScriptComp.Destroy = [&]()
			{
				delete newScriptComp.Instance;
				newScriptComp.Instance = nullptr;
			};*/
		}
		if (entity.HasComponent<StaticMeshComponent>())
		{
			newEntity.AddComponent<StaticMeshComponent>(entity.GetComponent<StaticMeshComponent>());
		}
		if (entity.HasComponent<ModelComponent>())
		{
			newEntity.AddComponent<ModelComponent>(entity.GetComponent<ModelComponent>());
		}
		if (entity.HasComponent<RigidBody3DComponent>())
		{
			newEntity.AddComponent<RigidBody3DComponent>(entity.GetComponent<RigidBody3DComponent>());
			auto& rigidBody = newEntity.GetComponent<RigidBody3DComponent>();
			rigidBody.RuntimeBody = nullptr;
		}
		if (entity.HasComponent<BoxCollider3DComponent>())
		{
			newEntity.AddComponent<BoxCollider3DComponent>(entity.GetComponent<BoxCollider3DComponent>());
		}
		if (entity.HasComponent<SphereCollider3DComponent>())
		{
			newEntity.AddComponent<SphereCollider3DComponent>(entity.GetComponent<SphereCollider3DComponent>());
		}
		if (entity.HasComponent<CapsuleCollider3DComponent>())
		{
			newEntity.AddComponent<CapsuleCollider3DComponent>(entity.GetComponent<CapsuleCollider3DComponent>());
		}
		if (entity.HasComponent<MeshCollider3DComponent>())
		{
			newEntity.AddComponent<MeshCollider3DComponent>(entity.GetComponent<MeshCollider3DComponent>());
		}
		if (entity.HasComponent<DirectionalLightComponent>())
		{
			newEntity.AddComponent<DirectionalLightComponent>(entity.GetComponent<DirectionalLightComponent>());
		}
		if (entity.HasComponent<PointLightComponent>())
		{
			newEntity.AddComponent<PointLightComponent>(entity.GetComponent<PointLightComponent>());
		}
		if (entity.HasComponent<SpotLightComponent>())
		{
			newEntity.AddComponent<SpotLightComponent>(entity.GetComponent<SpotLightComponent>());
		}
		if (entity.HasComponent<EnvironmentComponent>())
		{
			newEntity.AddComponent<EnvironmentComponent>(entity.GetComponent<EnvironmentComponent>());
		}
		if (entity.HasComponent<TextComponent>())
		{
			newEntity.AddComponent<TextComponent>(entity.GetComponent<TextComponent>());
		}
		if (entity.HasComponent<AudioSource2DComponent>())
		{
			newEntity.AddComponent<AudioSource2DComponent>(entity.GetComponent<AudioSource2DComponent>());
		}
		if (entity.HasComponent<AudioSource3DComponent>())
		{
			newEntity.AddComponent<AudioSource3DComponent>(entity.GetComponent<AudioSource3DComponent>());
		}
		if (entity.HasComponent<AudioListenerComponent>())
		{
			newEntity.AddComponent<AudioListenerComponent>(entity.GetComponent<AudioListenerComponent>());
		}

		if (duplicateChildren)
		{
			const auto children = GetChildren(entity);
			for (const auto& child : children)
			{
				const Entity duplicatedChild = DuplicateEntity(child, true);
				SetParent(duplicatedChild, newEntity, false);
			}
		}

		// The duplicated entity will have the same parent as the original entity
		const UUID originalParentUUID = entity.GetComponent<HierarchyComponent>().Parent;
		if (originalParentUUID.IsValid())
		{
			const Entity originalParent = GetEntityByUUID(originalParentUUID);
			SetParent(newEntity, originalParent, false);
		}

		return newEntity;
	}

	void Scene::CreateChild(const Entity entity)
	{
		const Entity child = CreateEntity("Unnamed");
		SetParent(child, entity);
	}

	Entity Scene::InstantiateModelAsset(const AssetHandle modelHandle, const std::string& rootName)
	{
		if (!modelHandle.IsValid())
		{
			KBR_CORE_ERROR("Cannot instantiate model with invalid handle.");
			return {};
		}

		if (AssetManager::GetAssetType(modelHandle) != AssetType::Model)
		{
			KBR_CORE_ERROR("Asset {} is not a model asset.", modelHandle);
			return {};
		}

		const Ref<Model> model = AssetManager::GetAsset<Model>(modelHandle);
		if (!model)
		{
			KBR_CORE_ERROR("Failed to load model asset: {}", modelHandle);
			return {};
		}

		const std::string resolvedRootName = rootName.empty() ? model->GetName() : rootName;
		Entity modelRoot = CreateEntity(resolvedRootName.empty() ? "Model" : resolvedRootName);
		modelRoot.AddComponent<ModelComponent>(modelHandle);

		const auto& modelNodes = model->GetNodes();
		const auto& modelPrimitives = model->GetPrimitives();

		if (modelNodes.empty())
		{
			for (size_t primitiveIndex = 0; primitiveIndex < modelPrimitives.size(); ++primitiveIndex)
			{
				const auto& primitive = modelPrimitives[primitiveIndex];
				Entity primitiveEntity = CreateEntity(primitive.Name.empty()
					? "Primitive_" + std::to_string(primitiveIndex)
					: primitive.Name);
				SetParent(primitiveEntity, modelRoot, false);

				auto& smc = primitiveEntity.AddComponent<StaticMeshComponent>();
				smc.StaticMesh = primitive.Mesh;
				smc.MeshMaterial = primitive.Material;
			}

			return modelRoot;
		}

		std::vector<Entity> nodeEntities(modelNodes.size());

		for (size_t nodeIndex = 0; nodeIndex < modelNodes.size(); ++nodeIndex)
		{
			const auto& node = modelNodes[nodeIndex];
			Entity nodeEntity = CreateEntity(node.Name.empty() ? "Node_" + std::to_string(nodeIndex) : node.Name);
			nodeEntities[nodeIndex] = nodeEntity;

			auto& transform = nodeEntity.GetComponent<TransformComponent>();
			transform.Translation = node.Translation;
			transform.Rotation = glm::eulerAngles(node.Rotation);
			transform.Scale = node.Scale;

			bool assignedPrimaryPrimitive = false;
			for (size_t primitiveSlot = 0; primitiveSlot < node.PrimitiveIndices.size(); ++primitiveSlot)
			{
				const uint32_t primitiveIndex = node.PrimitiveIndices[primitiveSlot];
				if (primitiveIndex >= modelPrimitives.size())
					continue;

				const auto& primitive = modelPrimitives[primitiveIndex];
				if (!assignedPrimaryPrimitive)
				{
					auto& smc = nodeEntity.AddComponent<StaticMeshComponent>();
					smc.StaticMesh = primitive.Mesh;
					smc.MeshMaterial = primitive.Material;
					assignedPrimaryPrimitive = true;
					continue;
				}

				Entity primitiveEntity = CreateEntity(primitive.Name.empty()
					? nodeEntity.GetComponent<TagComponent>().Tag + "_Primitive_" + std::to_string(primitiveSlot)
					: primitive.Name);
				SetParent(primitiveEntity, nodeEntity, false);

				auto& smc = primitiveEntity.AddComponent<StaticMeshComponent>();
				smc.StaticMesh = primitive.Mesh;
				smc.MeshMaterial = primitive.Material;
			}
		}

		for (size_t nodeIndex = 0; nodeIndex < modelNodes.size(); ++nodeIndex)
		{
			const auto& node = modelNodes[nodeIndex];
			const Entity nodeEntity = nodeEntities[nodeIndex];
			if (!nodeEntity)
				continue;

			if (node.ParentIndex >= 0 && std::cmp_less(node.ParentIndex, nodeEntities.size()))
				SetParent(nodeEntity, nodeEntities[node.ParentIndex], false);
			else
				SetParent(nodeEntity, modelRoot, false);
		}

		return modelRoot;
	}

	Entity Scene::GetEntityByUUID(const UUID uuid) const
	{
		KBR_PROFILE_FUNCTION();

#if USE_MAP_FOR_UUID

		return m_UUIDToEntityMap.at(uuid);
#else
		const auto view = m_Registry.view<IDComponent>();
		for (auto entity : view)
		{
			auto id = view.get<IDComponent>(entity).ID;
			if (id == uuid)
			{
				return { entity, this };
			}
		}

		return {};
#endif
	}

	void Scene::SetParent(const Entity child, const Entity parent, bool keepWorldTransform)
	{
		RemoveParent(child);

		if (const auto it = std::ranges::find(m_RootEntities, static_cast<entt::entity>(child)); it != m_RootEntities.end())
		{
			m_RootEntities.erase(it);
		}

		auto& childHierarchy = child.GetComponent<HierarchyComponent>();
		auto& parentHierarchy = parent.GetComponent<HierarchyComponent>();

		childHierarchy.Parent = parent.GetUUID();
		parentHierarchy.Children.emplace_back(child.GetUUID());
		
		// Recalculate the child's transform and all its descendants based on new parent
		CalculateEntityTransform(child);
	}

	Entity Scene::GetParent(const Entity child) const
	{
		KBR_PROFILE_FUNCTION();

		const auto& childHierarchy = child.GetComponent<HierarchyComponent>();
		if (childHierarchy.Parent.IsValid())
		{
			return GetEntityByUUID(childHierarchy.Parent);
		}
		return {};
	}

	void Scene::RemoveParent(const Entity child)
	{
		KBR_PROFILE_FUNCTION();

		auto& childHierarchy = child.GetComponent<HierarchyComponent>();
		if (childHierarchy.Parent.IsValid())
		{
			auto& parentHierarchy = GetEntityByUUID(childHierarchy.Parent).GetComponent<HierarchyComponent>();
			const auto it = std::ranges::find(parentHierarchy.Children, child.GetUUID());
			if (it != parentHierarchy.Children.end())
			{
				parentHierarchy.Children.erase(it);
			}

			childHierarchy.Parent = UUID::Invalid();
			/// The child is a root entity now
			m_RootEntities.push_back(static_cast<entt::entity>(child));
			
			// Recalculate the child's transform and all its descendants since it's now a root entity
			CalculateEntityTransform(child);
		}
	}

	std::vector<Entity> Scene::GetChildren(const Entity parent) const
	{
		KBR_PROFILE_FUNCTION();

		const auto& parentHierarchy = parent.GetComponent<HierarchyComponent>();

		std::vector<Entity> children;
		for (const UUID& childId : parentHierarchy.Children)
		{
			Entity entity = GetEntityByUUID(childId);
			children.push_back(entity);
		}
		return children;
	}

	void Scene::OnViewportResize(const uint32_t width, const uint32_t height)
	{
		m_ViewportHeight = height;
		m_ViewportWidth = width;

		if (m_ViewportWidth == 0 || m_ViewportHeight == 0)
		{
			return;
		}

		/// Resize the non-fixed aspect ratio cameras
		const auto view = m_Registry.view<CameraComponent>();
		for (const auto entity : view)
		{
			auto& cameraComponent = view.get<CameraComponent>(entity);
			if (!cameraComponent.FixedAspectRatio)
			{
				cameraComponent.Camera.SetViewportSize(width, height);
			}
		}

		Renderer::ResizeResources(width, height);
	}

	const PhysicsSystem& Scene::GetPhysicsSystem() const
	{
		KBR_CORE_ASSERT(m_PhysicsSystem, "Physics system is not initialized");
		return *m_PhysicsSystem;
	}

	PhysicsSystem& Scene::GetPhysicsSystem()
	{
		KBR_CORE_ASSERT(m_PhysicsSystem, "Physics system is not initialized");
		return *m_PhysicsSystem;
	}

	void Scene::CopyEntityRecursive(const Ref<Scene>& other, const entt::entity sourceEntity, const Ref<Scene>& newScene, const entt::entity parentNewEntity)
	{
		auto& sourceRegistry = other->m_Registry;

		const UUID sourceID = sourceRegistry.get<IDComponent>(sourceEntity).ID;
		const auto& tag = sourceRegistry.get<TagComponent>(sourceEntity).Tag;
		Entity newEntity = newScene->CreateEntityWithUUID(tag, sourceID);

		/// Copy all components

		newEntity.GetComponent<TransformComponent>() = sourceRegistry.get<TransformComponent>(sourceEntity);

		if (sourceRegistry.all_of<SpriteRendererComponent>(sourceEntity))
			newEntity.AddComponent<SpriteRendererComponent>(sourceRegistry.get<SpriteRendererComponent>(sourceEntity));
		if (sourceRegistry.all_of<CameraComponent>(sourceEntity))
		{
			auto& cameraComp = sourceRegistry.get<CameraComponent>(sourceEntity);
			newEntity.AddComponent<CameraComponent>(cameraComp);
			newEntity.GetComponent<CameraComponent>().Camera.SetViewportSize(newScene->m_ViewportWidth, newScene->m_ViewportHeight);
		}
		if (sourceRegistry.all_of<ScriptComponent>(sourceEntity))
			newEntity.AddComponent<ScriptComponent>(sourceRegistry.get<ScriptComponent>(sourceEntity));
		if (sourceRegistry.all_of<StaticMeshComponent>(sourceEntity))
			newEntity.AddComponent<StaticMeshComponent>(sourceRegistry.get<StaticMeshComponent>(sourceEntity));
		if (sourceRegistry.all_of<ModelComponent>(sourceEntity))
			newEntity.AddComponent<ModelComponent>(sourceRegistry.get<ModelComponent>(sourceEntity));
		if (sourceRegistry.all_of<RigidBody3DComponent>(sourceEntity))
		{
			auto& rigidBodyComp = sourceRegistry.get<RigidBody3DComponent>(sourceEntity);
			newEntity.AddComponent<RigidBody3DComponent>(rigidBodyComp);
			newEntity.GetComponent<RigidBody3DComponent>().RuntimeBody = nullptr;
		}
		if (sourceRegistry.all_of<BoxCollider3DComponent>(sourceEntity))
			newEntity.AddComponent<BoxCollider3DComponent>(sourceRegistry.get<BoxCollider3DComponent>(sourceEntity));
		if (sourceRegistry.all_of<SphereCollider3DComponent>(sourceEntity))
			newEntity.AddComponent<SphereCollider3DComponent>(sourceRegistry.get<SphereCollider3DComponent>(sourceEntity));
		if (sourceRegistry.all_of<CapsuleCollider3DComponent>(sourceEntity))
			newEntity.AddComponent<CapsuleCollider3DComponent>(sourceRegistry.get<CapsuleCollider3DComponent>(sourceEntity));
		if (sourceRegistry.all_of<MeshCollider3DComponent>(sourceEntity))
			newEntity.AddComponent<MeshCollider3DComponent>(sourceRegistry.get<MeshCollider3DComponent>(sourceEntity));
		if (sourceRegistry.all_of<DirectionalLightComponent>(sourceEntity))
			newEntity.AddComponent<DirectionalLightComponent>(sourceRegistry.get<DirectionalLightComponent>(sourceEntity));
		if (sourceRegistry.all_of<PointLightComponent>(sourceEntity))
			newEntity.AddComponent<PointLightComponent>(sourceRegistry.get<PointLightComponent>(sourceEntity));
		if (sourceRegistry.all_of<SpotLightComponent>(sourceEntity))
			newEntity.AddComponent<SpotLightComponent>(sourceRegistry.get<SpotLightComponent>(sourceEntity));
		if (sourceRegistry.all_of<EnvironmentComponent>(sourceEntity))
			newEntity.AddComponent<EnvironmentComponent>(sourceRegistry.get<EnvironmentComponent>(sourceEntity));
		if (sourceRegistry.all_of<TextComponent>(sourceEntity))
			newEntity.AddComponent<TextComponent>(sourceRegistry.get<TextComponent>(sourceEntity));
		if (sourceRegistry.all_of<AudioSource2DComponent>(sourceEntity))
			newEntity.AddComponent<AudioSource2DComponent>(sourceRegistry.get<AudioSource2DComponent>(sourceEntity));
		if (sourceRegistry.all_of<AudioSource3DComponent>(sourceEntity))
			newEntity.AddComponent<AudioSource3DComponent>(sourceRegistry.get<AudioSource3DComponent>(sourceEntity));
		if (sourceRegistry.all_of<AudioListenerComponent>(sourceEntity))
			newEntity.AddComponent<AudioListenerComponent>(sourceRegistry.get<AudioListenerComponent>(sourceEntity));

		/// Set parent if this is a child entity
		if (parentNewEntity != entt::null)
			newScene->SetParent(newEntity, Entity{ parentNewEntity, newScene.get() });

		/// Recursively copy children in order
		if (sourceRegistry.all_of<HierarchyComponent>(sourceEntity))
		{
			const auto& sourceHierarchy = sourceRegistry.get<HierarchyComponent>(sourceEntity);
			for (const UUID& childSourceID : sourceHierarchy.Children)
			{
				Entity childSource = other->GetEntityByUUID(childSourceID);
				CopyEntityRecursive(other, static_cast<entt::entity>(childSource), newScene, static_cast<entt::entity>(newEntity));
			}
		}
	}

	Ref<Scene> Scene::Copy(const Ref<Scene>& other)
	{
		Ref<Scene> newScene = CreateRef<Scene>();

		newScene->SetName(other->m_Name);

		newScene->m_ViewportWidth = other->m_ViewportWidth;
		newScene->m_ViewportHeight = other->m_ViewportHeight;

		/// Copy root entities first preserving order, then recursively copy children
		for (const auto sourceRootEntity : other->m_RootEntities)
		{
			CopyEntityRecursive(other, sourceRootEntity, newScene, entt::null);
		}

		return newScene;
	}

	AssetType Scene::GetType()
	{
		return AssetType::Scene;
	}


	void Scene::Render2DRuntime(const SceneCamera* mainCamera, const glm::mat4& mainCameraTransform, const float dt)
	{
		//Renderer2D::BeginScene(*mainCamera, mainCameraTransform);

		const auto view = m_Registry.view<TransformComponent, SpriteRendererComponent>();
		for (const auto entity : view)
		{
			[[maybe_unused]] auto [transform, sprite] = view.get<TransformComponent, SpriteRendererComponent>(entity);

			//Renderer2D::DrawQuad(transform.WorldTransform, sprite.Color);
		}

		//Renderer2D::EndScene();
	}

	void Scene::Render3DRuntime(const SceneCamera* mainCamera, const glm::mat4& mainCameraTransform, const float dt)
	{
		KBR_PROFILE_FUNCTION();
#pragma region old_rendering_code
		//DirectionalLightComponent* dlc = nullptr;
		//const auto sunView = m_Registry.view<DirectionalLightComponent, TransformComponent>();
		//for (const auto entity : sunView)
		//{
		//	auto [light, transform] = sunView.get<DirectionalLightComponent, TransformComponent>(entity);
		//	if (light.IsEnabled)
		//	{
		//		dlc = &light;
		//		break;
		//	}
		//}

		//if (ShouldRenderShadows(dlc))
		//{
		//	/*ShadowMapSettings shadowSettings;
		//	shadowSettings.Resolution = 1024;
		//	shadowSettings.OrthoSize = 15.0f;
		//	shadowSettings.NearPlane = 1.0f;
		//	shadowSettings.FarPlane = 100.0f;
		//	shadowSettings.EnableShadows = true;

		//	Renderer3D::BeginShadowPass(dlc->Light, shadowSettings, m_ShadowMapFramebuffer);*/

		//	/// Render all shadow-casting meshes
		//	const auto meshView = m_Registry.view<StaticMeshComponent, TransformComponent>();
		//	for (auto entity : meshView)
		//	{
		//		auto& meshComp = meshView.get<StaticMeshComponent>(entity);
		//		auto& transformComp = meshView.get<TransformComponent>(entity);

		//		if (meshComp.StaticMesh && meshComp.MeshMaterial && meshComp.Visible)
		//		{
		//			/*Renderer3D::SubmitMesh(meshComp.StaticMesh, transformComp.WorldTransform,
		//								   meshComp.MeshMaterial, meshComp.MeshTexture, 1.0f,
		//								   static_cast<int>(entity), meshComp.CastShadows);*/
		//		}
		//	}

		//	dlc->NeedsUpdate = false;

		//	//Renderer3D::EndPass();
		//}

		//std::vector<PointLight> pointLights;
		//const auto pointLightView = m_Registry.view<PointLightComponent, TransformComponent>();
		//for (const auto entity : pointLightView)
		//{
		//	auto [light, transform] = pointLightView.get<PointLightComponent, TransformComponent>(entity);
		//	if (light.IsEnabled)
		//	{
		//		pointLights.push_back(light.Light);
		//	}
		//}

		//Ref<TextureCube> skyboxTexture = nullptr;
		//const auto skyboxView = m_Registry.view<EnvironmentComponent>();
		//for (const auto entity : skyboxView)
		//{
		//	const auto& skybox = skyboxView.get<EnvironmentComponent>(entity);
		//	if (skybox.IsSkyboxEnabled && skybox.SkyboxTexture)
		//	{
		//		skyboxTexture = AssetManager::GetAsset<TextureCube>(skybox.SkyboxTexture);
		//		break;
		//	}
		//}

		///*m_EditorFramebuffer->Bind();

		//RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		//RenderCommand::Clear();

		///// Clear our entity ID attachment to -1, so when rendering entities they fill that space with their entity ID,
		///// and empty spacces will have -1, signaling that there is no entity.
		///// Used for mouse picking.
		//m_EditorFramebuffer->ClearAttachment(1, -1);

		//Renderer3D::BeginGeometryPass(*mainCamera, mainCameraTransform, &dlc->Light, pointLights, skyboxTexture);*/

		//const auto view = m_Registry.view<TransformComponent, StaticMeshComponent>();
		//for (const auto entity : view)
		//{
		//	auto [transform, mesh] = view.get<TransformComponent, StaticMeshComponent>(entity);

		//	if (mesh.Visible)
		//	{
		//		//Renderer3D::SubmitMesh(mesh.StaticMesh, transform.WorldTransform, mesh.MeshMaterial, mesh.MeshTexture);
		//	}
		//}

		////Renderer3D::EndPass();

		//const auto textView = m_Registry.view<TransformComponent, TextComponent>();
		//for (const auto entity : textView)
		//{
		//	auto [transform, text] = textView.get<TransformComponent, TextComponent>(entity);

		//	//Renderer3D::SubmitText(text.Text, text.Font, transform.WorldTransform, text.Color, text.FontSize, static_cast<int>(entity));
		//}

		////Renderer3D::EndScene();

#pragma endregion

		throw std::runtime_error("Not implemented");
		//Renderer::RenderSceneRuntime(shared_from_this(), mainCamera, mainCameraTransform);
	}

	void Scene::Render3DRuntime(const Camera& camera, const float dt) 
	{
		glm::mat4 cameraTransform(1.0f);
		const glm::vec4 camPos = { camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z, 1.0f };
		cameraTransform[3] = camPos;
		Renderer::RenderSceneRuntime(shared_from_this(), camera, cameraTransform, dt);
	}

	void Scene::Render3DEditor(const Camera& camera, const float dt)
	{
#pragma region old_rendering_code
		//DirectionalLightComponent* dlc = nullptr;
		//const auto sunView = m_Registry.view<DirectionalLightComponent, TransformComponent>();
		//for (const auto entity : sunView)
		//{
		//	auto [light, transform] = sunView.get<DirectionalLightComponent, TransformComponent>(entity);
		//	if (light.IsEnabled)
		//	{
		//		dlc = &light;
		//		break;
		//	}
		//}

		//if (ShouldRenderShadows(dlc))
		//{
		//	/*ShadowMapSettings shadowSettings;
		//	shadowSettings.Resolution = 1024;
		//	shadowSettings.OrthoSize = 15.0f;
		//	shadowSettings.NearPlane = 1.0f;
		//	shadowSettings.FarPlane = 100.0f;
		//	shadowSettings.EnableShadows = true;

		//	Renderer3D::BeginShadowPass(dlc->Light, shadowSettings, m_ShadowMapFramebuffer);*/

		//	/// Render all shadow-casting meshes
		//	const auto meshView = m_Registry.view<StaticMeshComponent, TransformComponent>();
		//	for (auto entity : meshView)
		//	{
		//		auto& meshComp = meshView.get<StaticMeshComponent>(entity);
		//		auto& transformComp = meshView.get<TransformComponent>(entity);

		//		if (meshComp.StaticMesh && meshComp.MeshMaterial && meshComp.Visible)
		//		{
		//			/*Renderer3D::SubmitMesh(meshComp.StaticMesh, transformComp.WorldTransform,
		//								   meshComp.MeshMaterial, meshComp.MeshTexture, 1.0f,
		//								   static_cast<int>(entity), meshComp.CastShadows);*/
		//		}
		//	}

		//	dlc->NeedsUpdate = false;

		//	//Renderer3D::EndPass();
		//}

		//std::vector<PointLight> pointLights;
		//const auto pointLightView = m_Registry.view<PointLightComponent, TransformComponent>();
		//for (const auto entity : pointLightView)
		//{
		//	auto [light, transform] = pointLightView.get<PointLightComponent, TransformComponent>(entity);
		//	if (light.IsEnabled)
		//	{
		//		pointLights.push_back(light.Light);
		//	}
		//}

		//Ref<TextureCube> skyboxTexture = nullptr;
		//const auto skyboxView = m_Registry.view<EnvironmentComponent>();
		//for (const auto entity : skyboxView)
		//{
		//	const auto& skybox = skyboxView.get<EnvironmentComponent>(entity);
		//	if (skybox.IsSkyboxEnabled && skybox.SkyboxTexture)
		//	{
		//		skyboxTexture = AssetManager::GetAsset<TextureCube>(skybox.SkyboxTexture);
		//		break;
		//	}
		//}

		///*m_EditorFramebuffer->Bind();

		//RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		//RenderCommand::Clear();*/

		///// Clear our entity ID attachment to -1, so when rendering entities they fill that space with their entity ID,
		///// and empty spacces will have -1, signaling that there is no entity.
		///// Used for mouse picking.
		///*m_EditorFramebuffer->ClearAttachment(1, -1);

		//Renderer3D::BeginGeometryPass(camera, &dlc->Light, pointLights, skyboxTexture);*/

		//const auto view = m_Registry.view<TransformComponent, StaticMeshComponent>();
		//for (const auto entity : view)
		//{
		//	auto [transform, mesh] = view.get<TransformComponent, StaticMeshComponent>(entity);

		//	if (mesh.Visible)
		//	{
		//		//Renderer3D::SubmitMesh(mesh.StaticMesh, transform.WorldTransform, mesh.MeshMaterial, mesh.MeshTexture, 1.0f, static_cast<int>(entity), mesh.CastShadows);
		//	}
		//}

		////Renderer3D::EndPass();

		//const auto textView = m_Registry.view<TransformComponent, TextComponent>();
		//for (const auto entity : textView)
		//{
		//	auto [transform, text] = textView.get<TransformComponent, TextComponent>(entity);

		//	//Renderer3D::SubmitText(text.Text, text.Font, transform.WorldTransform, text.Color, text.FontSize, static_cast<int>(entity));
		//}

		////Renderer3D::EndScene();
#pragma endregion
		Renderer::RenderSceneEditor(shared_from_this(), camera, dt);
	}

	void Scene::UpdateScripts(float ts)
	{
		KBR_PROFILE_FUNCTION();

		{
			KBR_PROFILE_SCOPE("Scene::UpdateScripts - Native scripts update");

			m_Registry.view<NativeScriptComponent>().each([this]([[maybe_unused]] auto entity, [[maybe_unused]] const NativeScriptComponent& script)
			{
				/*if (!script.Instance)
				{
					script.Instantiate();
					script.Instance->m_Entity = Entity{ entity, this };
					script.Instance->OnCreate();
				}

				script.Instance->OnUpdate(ts);*/
			});
		}

		{
			KBR_PROFILE_SCOPE("Scene::UpdateScripts - C# scripts update");

			m_Registry.view<ScriptComponent>().each([this, ts](auto id, [[maybe_unused]] const ScriptComponent& script)
			{
				const Entity entity{ id, this };
				ScriptEngine::OnUpdateEntity(entity, ts);
			});
		}
	}

	void Scene::UpdateChildTransforms(const Entity parent, const glm::mat4& parentTransform)
	{
		auto& tsc = parent.GetComponent<TransformComponent>();
		const glm::mat4 localTransform = tsc.GetTransform();
		tsc.WorldTransform = parentTransform * localTransform;

		//tsc.Translation = Physics::ExtractTranslationFromMatrix(tsc.WorldTransform);

		for (const auto& child : GetChildren(parent))
		{
			UpdateChildTransforms(child, tsc.WorldTransform);
		}
	}

	bool Scene::ShouldRenderShadows(const DirectionalLightComponent* dlc) const
	{
		if (m_OnlyRenderShadowMapIfLightHasChanged)
		{
			return m_EnableShadowMapping && dlc && dlc->IsEnabled && dlc->CastShadows && dlc->NeedsUpdate;
		}
		return m_EnableShadowMapping && dlc && dlc->IsEnabled && dlc->CastShadows;
	}

	DirectionalLight Scene::GetSunlight() const 
	{
		// Default sunlight pointing upwards, in case there is no directional light in the scene
		DirectionalLight sunlight{ .Direction = glm::vec3(0.0f, -1.0f, 0.0f) };
		const auto sunView = m_Registry.view<DirectionalLightComponent, TransformComponent>();
		bool found = false;
		for (const auto entity : sunView)
		{
			auto [light, transform] = sunView.get<DirectionalLightComponent, TransformComponent>(entity);
			if (light.IsEnabled)
			{
				if (found)
				{
					KBR_CORE_WARN("Multiple directional lights found in the scene. Using the first one found as sunlight.");
					break;
				}

				sunlight = light.Light;
				found = true;
			}
		}
		return sunlight;
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		const auto view = m_Registry.view<CameraComponent>();
		for (const auto entity : view)
		{
			const auto& camera = view.get<CameraComponent>(entity);
			if (camera.IsPrimary)
			{
				return Entity{ entity, this };
			}
		}

		return {};
	}

	void Scene::CalculateEntityTransforms()
	{
		KBR_PROFILE_FUNCTION();

		const auto view = m_Registry.view<TransformComponent>();
		for (const auto id : view)
		{
			const Entity entity{ id, this };
			const UUID parentUUID = entity.GetComponent<HierarchyComponent>().Parent;

			/// Only calculate the transforms of root entities, since the UpdateChildTransforms
			/// function will handle all the children
			if (!parentUUID.IsValid())
			{
				UpdateChildTransforms(entity, glm::mat4(1.0f));
			}
		}
	}

	void Scene::CalculateEntityTransform(const Entity& entity)
	{
		const UUID parentUUID = entity.GetComponent<HierarchyComponent>().Parent;

		/*const glm::mat4 parentTransform = parentUUID.IsValid()
			? GetEntityByUUID(parentUUID).GetComponent<TransformComponent>().WorldTransform
			: glm::mat4(1.0f);*/
		
		// If this entity has a parent, ensure the parent's transform is up-to-date first
		if (parentUUID.IsValid())
		{
			const Entity parent = GetEntityByUUID(parentUUID);
			// Recursively ensure all ancestors are updated first
			CalculateEntityTransform(parent);
			
			// Now use the parent's freshly updated WorldTransform
			const glm::mat4 parentTransform = parent.GetComponent<TransformComponent>().WorldTransform;
			UpdateChildTransforms(entity, parentTransform);
		}
		else
		{
			// This is a root entity, use identity as parent transform
			UpdateChildTransforms(entity, glm::mat4(1.0f));
		}
	}

	Entity Scene::FindEntityByName(const std::string_view name)
	{
		const auto view = m_Registry.view<TagComponent>();
		for (const auto entity : view)
		{
			const auto& tag = view.get<TagComponent>(entity);
			if (tag.Tag == name)
			{
				return Entity{ entity, this };
			}
		}
		return {};
	}

#pragma region ComponentAddedSpecializations

	template <typename T>
	void Scene::OnComponentAdded(Entity /*entity*/, T& /*component*/)
	{
		static_assert(sizeof(T) == 0, "No template specialization found for this type");
	}

	template <>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
	{
		component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}

	template <>
	void Scene::OnComponentAdded<IDComponent>(Entity, IDComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<TransformComponent>(Entity, TransformComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<SkinComponent>(Entity, SkinComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<SkeletalMeshComponent>(Entity, SkeletalMeshComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<AnimationComponent>(Entity, AnimationComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<TagComponent>(Entity, TagComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<SpriteRendererComponent>(Entity, SpriteRendererComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<ScriptComponent>(Entity, ScriptComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<NativeScriptComponent>(Entity, NativeScriptComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<StaticMeshComponent>(Entity, StaticMeshComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<ModelComponent>(Entity, ModelComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<DirectionalLightComponent>(Entity, DirectionalLightComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<PointLightComponent>(Entity, PointLightComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<SpotLightComponent>(Entity, SpotLightComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<HierarchyComponent>(Entity, HierarchyComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<RigidBody3DComponent>(Entity, RigidBody3DComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<BoxCollider3DComponent>(const Entity entity, BoxCollider3DComponent&)
	{
		if (entity.HasComponent<RigidBody3DComponent>())
		{
			entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
		}
	}

	template <>
	void Scene::OnComponentAdded<SphereCollider3DComponent>(const Entity entity, SphereCollider3DComponent&)
	{
		if (entity.HasComponent<RigidBody3DComponent>())
		{
			entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
		}
	}

	template <>
	void Scene::OnComponentAdded<CapsuleCollider3DComponent>(const Entity entity, CapsuleCollider3DComponent&)
	{
		if (entity.HasComponent<RigidBody3DComponent>())
		{
			entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
		}
	}

	template <>
	void Scene::OnComponentAdded<MeshCollider3DComponent>(const Entity entity, MeshCollider3DComponent& component)
	{
		const auto& smc = entity.GetComponent<StaticMeshComponent>();
		if (smc.StaticMesh == nullptr)
		{
			KBR_CORE_WARN("MeshCollider3DComponent on entity {} does not have a valid static mesh!", entity.GetComponent<TagComponent>().Tag);
			return;
		}

		component.Mesh = smc.StaticMesh;

		if (entity.HasComponent<RigidBody3DComponent>())
		{
			entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
		}
	}

	template <>
	void Scene::OnComponentAdded<EnvironmentComponent>(Entity, EnvironmentComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<TextComponent>(Entity, TextComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<AudioSource3DComponent>(Entity, AudioSource3DComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<AudioSource2DComponent>(Entity, AudioSource2DComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<AudioListenerComponent>(Entity, AudioListenerComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<PrefabInstanceComponent>(Entity, PrefabInstanceComponent&)
	{
	}

	template <>
	void Scene::OnComponentAdded<ParticleEmitterComponent>(Entity, ParticleEmitterComponent&)
	{
	}

#pragma endregion
}
