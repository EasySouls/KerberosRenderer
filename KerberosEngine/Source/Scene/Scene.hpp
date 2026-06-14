#pragma once

#include "Components.hpp"
#include "Camera/EditorCamera.hpp"
#include "Camera/SceneCamera.hpp"
#include "Core/UUID.hpp"
#include "Physics/Jolt/JoltPhysicsSystem.hpp"
#include "Assets/Asset.hpp"

#include <entt/single_include/entt/entt.hpp>
#include <set>
#include <string_view>
#include <memory>

namespace Kerberos
{
	class Entity;
	class HierarchyPanel;

	class Scene : public std::enable_shared_from_this<Scene>, public Asset
	{
	public:
		Scene();
		~Scene() override;

		void OnRuntimeStart();
		void OnRuntimeStop() const;
		void OnSimulationStart();
		void OnSimulationStop() const;
		void SetScenePaused(bool isPaused);

		void OnUpdateEditor(float ts, const Camera& camera);
		void OnUpdateSimulation(float ts, const Camera& camera);
		void OnUpdateRuntime(float ts, const Camera& camera);

		/**
		 * @brief Create an entity in the scene and assigns it a transform component
		 *
		 * @return Entity The entity created
		 */
		Entity CreateEntity(const std::string& name = std::string(), UUID parentId = UUID::Invalid());

		Entity CreateEntityWithUUID(const std::string& name, uint64_t uuid);

		/**
		 * @brief Destroy an entity in the scene
		 *
		 * @param entity The entity to destroy
		 */
		void DestroyEntity(Entity entity);

		/**
		 * @brief Duplicates an entity in the scene
		 *
		 * It new entity gets the associated assets, so if the assets are modified, the duplicated entity will reflect those changes.
		 *
		 * @param entity The entity to duplicate
		 * @param duplicateChildren If true, duplicates the children of the entity as well
		 * @return Entity The duplicated entity
		 */
		Entity DuplicateEntity(Entity entity, bool duplicateChildren);

		void CreateChild(Entity entity);
		Entity InstantiateModelAsset(AssetHandle modelHandle, const std::string& rootName = std::string());
		Entity InstantiatePrefab(AssetHandle prefabHandle, const std::string& rootName = std::string());

		Entity GetEntityByUUID(UUID uuid) const;

		void SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
		Entity GetParent(Entity child) const;
		void RemoveParent(Entity child);
		std::vector<Entity> GetChildren(Entity parent) const;

		const std::vector<entt::entity>& GetRootEntities() const { return m_RootEntities; }

		void OnViewportResize(uint32_t width, uint32_t height);

		void SetIs3D(const bool is3D) { m_Is3D = is3D; }
		void SetEnableShadowMapping(const bool enable) { m_EnableShadowMapping = enable; }

		DirectionalLight GetSunlight() const;

		Entity GetPrimaryCameraEntity();
		void CalculateEntityTransforms();
		void CalculateEntityTransform(const Entity& entity);

		Entity FindEntityByName(std::string_view name);

		bool& GetOnlyRenderShadowMapIfLightHasChanged() { return m_OnlyRenderShadowMapIfLightHasChanged; }

		const PhysicsSystem& GetPhysicsSystem() const;
		PhysicsSystem& GetPhysicsSystem();

		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const { return m_Name; }

		static Ref<Scene> Copy(const Ref<Scene>& other);

		AssetType GetType() override;

	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

		void Render2DRuntime(const SceneCamera* mainCamera, const glm::mat4& mainCameraTransform);
		void Render3DRuntime(const SceneCamera* mainCamera, const glm::mat4& mainCameraTransform);
		void Render3DRuntime(const Camera& camera);
		void Render3DEditor(const Camera& camera);

		void UpdateScripts(float ts);

		void UpdateChildTransforms(Entity parent, const glm::mat4& parentTransform);

		bool ShouldRenderShadows(const DirectionalLightComponent* dlc) const;

		static void CopyEntityRecursive(const Ref<Scene>& other, entt::entity sourceEntity, const Ref<Scene>& newScene, entt::entity parentNewEntity);

		template<typename Component>
		static void CopyComponent(entt::registry& dst, entt::registry& src)
		{

		}

	private:
		std::string m_Name = "Untitled Scene";

		entt::registry m_Registry;

		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;

		bool m_IsScenePaused = false;

		bool m_Is3D = true;
		bool m_EnableShadowMapping = true;
		bool m_OnlyRenderShadowMapIfLightHasChanged = false;

		std::unordered_map<UUID, Entity> m_UUIDToEntityMap;

		std::vector<entt::entity> m_RootEntities;

		JoltPhysicsSystem* m_PhysicsSystem;

		friend class Entity;
		friend class Renderer;
		friend class JoltPhysicsSystem;
		friend class HierarchyPanel;
		friend class SceneSerializer;
		friend class RayTracingSceneCache;
	};
}
