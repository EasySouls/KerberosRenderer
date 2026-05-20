#include "kbrpch.hpp"
#include "ScriptInstance.hpp"

#include "ScriptClass.hpp"
#include "ScriptEngine.hpp"

namespace Kerberos
{
	char ScriptInstance::s_FieldValueBuffer[maxFieldSize] = {};

	ScriptInstance::ScriptInstance(const Ref<ScriptClass>& scriptClass, const Entity entity, const std::unordered_map<std::string, ScriptFieldInitializer>& initialFieldValues)
		: m_Entity(entity), m_EntityID(entity.GetUUID()), m_ScriptClass(scriptClass)
	{
		/// Create managed instance via the ScriptGlue bridge
		const bool created = ScriptEngine::CreateManagedInstance(
			static_cast<uint64_t>(m_EntityID),
			m_ScriptClass->GetFullName());

		KBR_CORE_ASSERT(created, "Failed to create managed instance for entity!");

		/// Set initial field values after creating the instance
		IntializeFieldValues(initialFieldValues);
	}

	void ScriptInstance::InvokeOnCreate() const
	{
		ScriptEngine::InvokeManagedOnCreate(static_cast<uint64_t>(m_EntityID));
	}

	void ScriptInstance::InvokeOnUpdate(const float deltaTime) const
	{
		ScriptEngine::InvokeManagedOnUpdate(static_cast<uint64_t>(m_EntityID), deltaTime);
	}

	void ScriptInstance::InvokeOnCollisionEnter(const CollisionEvent& event) const 
	{
		ScriptEngine::InvokeManagedOnCollisionEnter(static_cast<uint64_t>(m_EntityID), event);
	}

	void ScriptInstance::InvokeOnCollisionPersist(const CollisionEvent& event) const 
	{
		ScriptEngine::InvokeManagedOnCollisionPersist(static_cast<uint64_t>(m_EntityID), event);
	}

	void ScriptInstance::InvokeOnCollisionExit(const CollisionEvent& event) const 
	{
		ScriptEngine::InvokeManagedOnCollisionExit(static_cast<uint64_t>(m_EntityID), event);
	}

	void ScriptInstance::IntializeFieldValues(
		const std::unordered_map<std::string, ScriptFieldInitializer>& initialFieldValues) const
	{
		for (const auto& [name, initializer] : initialFieldValues)
		{
			SetFieldValueInternal(name, initializer.m_Data.data());
		}
	}

	bool ScriptInstance::GetFieldValueInternal(const std::string& name, void* buffer) const
	{
		if (!m_ScriptClass->m_SerializedFields.contains(name))
		{
			KBR_CORE_ASSERT(false, "Field {0} not found in class {1}", name, m_ScriptClass->GetFullName());
			return false;
		}

		return ScriptEngine::GetManagedFieldValue(
			static_cast<uint64_t>(m_EntityID),
			name, buffer, maxFieldSize);
	}

	void ScriptInstance::SetFieldValueInternal(const std::string& name, const void* value) const
	{
		if (!m_ScriptClass->m_SerializedFields.contains(name))
		{
			KBR_CORE_ASSERT(false, "Field {0} not found in class {1}", name, m_ScriptClass->GetFullName());
			return;
		}

		ScriptEngine::SetManagedFieldValue(
			static_cast<uint64_t>(m_EntityID),
			name, const_cast<void*>(value), maxFieldSize);
	}
}
