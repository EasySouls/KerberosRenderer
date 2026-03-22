#pragma once 

#include "Scene.hpp"
#include "Components.hpp"
#include "Logging/Log.hpp"
#include "Core/UUID.hpp"

#include <entt/single_include/entt/entt.hpp>

namespace Kerberos
{
	class Entity
	{
	public:
		Entity() = default;
		Entity(const entt::entity handle, const std::weak_ptr<Scene>& scene)
			: m_EntityHandle(handle), m_Scene(scene.lock().get())
		{
		}

		Entity(const entt::entity handle, Scene* scene)
			: m_EntityHandle(handle), m_Scene(scene)
		{
		}

		Entity(const Entity& other) = default;

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			KBR_CORE_ASSERT(!HasComponent<T>(), "Entity already has component!");
			T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
			m_Scene->OnComponentAdded<T>(*this, component);
			return component;
		}

		template<typename T>
		T& GetComponent() const
		{
			KBR_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");

			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		void RemoveComponent() const
		{
			KBR_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");

			m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}

		template<typename T>
		bool HasComponent() const
		{
			return m_Scene->m_Registry.any_of<T>(m_EntityHandle);
		}

		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		std::string_view GetName() const { return GetComponent<TagComponent>().Tag; }

		explicit(false) operator bool() const { return m_EntityHandle != entt::null; }

		explicit(false) operator entt::entity() const { return m_EntityHandle; }

		bool operator ==(const Entity& other) const
		{
			return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
		}

		bool operator !=(const Entity& other) const
		{
			return !(*this == other);
		}

	private:
		entt::entity m_EntityHandle{ entt::null };
		Scene* m_Scene = nullptr;
		//std::weak_ptr<Scene> m_Scene;
	};
}