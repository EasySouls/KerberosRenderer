#pragma once

#include "ScriptClass.hpp"

#include <string>
#include <string_view>

namespace Kerberos
{
	class ScriptUtils
	{
	public:
		/// Converts a .NET type name (e.g., "System.Single", "Kerberos.Source.Kerberos.Core.Vector3")
		/// to a ScriptFieldType enum value.
		static ScriptFieldType DotNetTypeToScriptFieldType(const std::string& typeName);

		static std::string_view ScriptFieldTypeToString(ScriptFieldType type);
		static ScriptFieldType StringToScriptFieldType(std::string_view type);
	};
}