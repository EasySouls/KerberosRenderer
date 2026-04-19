#pragma once

#include "ScriptClass.hpp"
#include "Scene/Entity.hpp"

#include <unordered_map>


namespace Kerberos
{
	class ScriptClass;

	class ScriptInstance
	{
	public:
		ScriptInstance(const Ref<ScriptClass>& scriptClass, Entity entity, const std::unordered_map<std::string, ScriptFieldInitializer>& initialFieldValues);

		void InvokeOnCreate() const;
		void InvokeOnUpdate(float deltaTime) const;

		template<typename T>
		T GetFieldValue(const std::string& name) const
		{
			static_assert(sizeof(T) <= maxFieldSize, "ScriptInstance can only get field types of size 40 or smaller");

			if (!GetFieldValueInternal(name, s_FieldValueBuffer)) {
				return T();
			}
			return *reinterpret_cast<T*>(s_FieldValueBuffer);
		}

		template<typename T>
		void SetFieldValue(const std::string& name, const T& value) const
		{
			static_assert(sizeof(T) <= maxFieldSize, "ScriptInstance can only set field types of size 40 or smaller");

			SetFieldValueInternal(name, &value);
		}

		const Ref<ScriptClass>& GetScriptClass() const { return m_ScriptClass; }

	private:
		void IntializeFieldValues(const std::unordered_map<std::string, ScriptFieldInitializer>& initialFieldValues) const;

		bool GetFieldValueInternal(const std::string& name, void* buffer) const;
		void SetFieldValueInternal(const std::string& name, const void* value) const;

	private:
		Entity m_Entity{};
		UUID m_EntityID = UUID::Invalid();
		Ref<ScriptClass> m_ScriptClass = nullptr;

		/// 40 is the size of the largest supported field type (std::string, double, long, ulong, vec4)
		/// When lists are supported, this will need to be changed
		static char s_FieldValueBuffer[maxFieldSize];
	};
}
