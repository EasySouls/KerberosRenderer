#pragma once

#include <string>
#include <array>
#include <cstddef>

namespace Kerberos
{
	class ScriptEngine;
	class ScriptInstance;

	// Usable field types in the editor for script components
	enum class ScriptFieldType : uint8_t
	{
		Short,
		Int,
		Long,
		UShort,
		UInt,
		ULong,
		Float,
		Double,
		Bool,
		Char,
		Byte,
		String,

		Vec2,
		Vec3,
		Vec4,

		AssetHandle
	};

	struct ScriptField
	{
		std::string Name;
		ScriptFieldType Type = ScriptFieldType::Int;
	};

	// 40 is the size of std::string on MSVC, which is the largest field type we have
	constexpr static size_t maxFieldSize = 40;

	struct ScriptFieldInitializer
	{
	public:
		ScriptField Field;

		template<typename T>
		T GetValue() const
			requires (sizeof(T) <= maxFieldSize)
		{
			return *reinterpret_cast<const T*>(m_Data.data());
		}

		template<typename T>
		void SetValue(const T& value)
			requires (sizeof(T) <= maxFieldSize)
		{
			std::memcpy(m_Data.data(), &value, sizeof(T));
		}

	private:
		mutable std::array<std::byte, maxFieldSize> m_Data = { static_cast<std::byte>('0') };

		friend class ScriptInstance;
	};

	/*
	* Represent a C# class in the scripting system.
	* In the .NET hosting model, class operations are delegated to the managed ScriptGlue bridge.
	*/
	class ScriptClass
	{
	public:
		ScriptClass() = default;
		ScriptClass(std::string classNamespace, std::string className, std::string fullName);

		const std::string& GetFullName() const { return m_FullName; }
		const std::unordered_map<std::string, ScriptField>& GetSerializedFields() const { return m_SerializedFields; }

	private:
		std::string m_ClassNamespace;
		std::string m_ClassName;
		std::string m_FullName;

		/// Fields that should be serialized and visible in the editor
		std::unordered_map<std::string, ScriptField> m_SerializedFields;

		friend class ScriptEngine;
		friend class ScriptInstance;
	};
}