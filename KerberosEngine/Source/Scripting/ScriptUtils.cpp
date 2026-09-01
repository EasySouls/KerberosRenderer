#include "ScriptUtils.hpp"

#include "ScriptClass.hpp"

#include <unordered_map>
#include <string_view>

import Kerberos;

using namespace std::literals;

namespace Kerberos
{
	static std::unordered_map<std::string, ScriptFieldType> s_ScriptFieldTypeMap = {
		{ "System.Int16",									ScriptFieldType::Short },
		{ "System.Int32",									ScriptFieldType::Int },
		{ "System.Int64",									ScriptFieldType::Long },
		{ "System.UInt16",								ScriptFieldType::UShort },
		{ "System.UInt32",								ScriptFieldType::UInt },
		{ "System.UInt64",								ScriptFieldType::ULong },
		{ "System.Single",								ScriptFieldType::Float },
		{ "System.Double",								ScriptFieldType::Double },
		{ "System.Boolean",								ScriptFieldType::Bool },
		{ "System.Char",									ScriptFieldType::Char },
		{ "System.Byte",									ScriptFieldType::Byte },
		{ "System.String",								ScriptFieldType::String },

		{ "Kerberos.Source.Kerberos.Core.Vector2",		ScriptFieldType::Vec2 },
		{ "Kerberos.Source.Kerberos.Core.Vector3",		ScriptFieldType::Vec3 },
		{ "Kerberos.Source.Kerberos.Core.Vector4",		ScriptFieldType::Vec4 },

		{ "Kerberos.Source.Kerberos.Core.MaterialRef",	ScriptFieldType::MaterialRef },
		{ "Kerberos.Source.Kerberos.Core.MeshRef",		ScriptFieldType::MeshRef },
		{ "Kerberos.Source.Kerberos.Core.TextureRef",		ScriptFieldType::TextureRef },
		{ "Kerberos.Source.Kerberos.Core.AssetHandle",	ScriptFieldType::AssetHandle },
	};

	ScriptFieldType ScriptUtils::DotNetTypeToScriptFieldType(const std::string& typeName)
	{
		if (s_ScriptFieldTypeMap.contains(typeName))
			return s_ScriptFieldTypeMap.at(typeName);

		Kerberos::Log::CoreWarn("Unrecognized .NET field type: {0}", typeName);
		return ScriptFieldType::Char;
	}

	std::string_view ScriptUtils::ScriptFieldTypeToString(const ScriptFieldType type)
	{
		switch (type)
		{
			case ScriptFieldType::Short:		return "Short"sv;
			case ScriptFieldType::Int:			return "Int"sv;
			case ScriptFieldType::Long:			return "Long"sv;
			case ScriptFieldType::UShort:		return "UShort"sv;
			case ScriptFieldType::UInt:			return "UInt"sv;
			case ScriptFieldType::ULong:		return "ULong"sv;
			case ScriptFieldType::Float:		return "Float"sv;
			case ScriptFieldType::Double:		return "Double"sv;
			case ScriptFieldType::Bool:			return "Bool"sv;
			case ScriptFieldType::Char:			return "Char"sv;
			case ScriptFieldType::Byte:			return "Byte"sv;
			case ScriptFieldType::String:		return "String"sv;
			case ScriptFieldType::Vec2:			return "Vector2"sv;
			case ScriptFieldType::Vec3:			return "Vector3"sv;
			case ScriptFieldType::Vec4:			return "Vector4"sv;
			case ScriptFieldType::MaterialRef:	return "MaterialRef"sv;
			case ScriptFieldType::MeshRef:		return "MeshRef"sv;
			case ScriptFieldType::TextureRef:	return "TextureRef"sv;
			case ScriptFieldType::AssetHandle:	return "AssetHandle"sv;
		}

		KBRAssert(false, "Unknown script field type!");
		return ""sv;
	}

	ScriptFieldType ScriptUtils::StringToScriptFieldType(const std::string_view type)
	{
		if (type == "Short"sv)			return ScriptFieldType::Short;
		if (type == "Int"sv)			return ScriptFieldType::Int;
		if (type == "Long"sv)			return ScriptFieldType::Long;
		if (type == "UShort"sv)			return ScriptFieldType::UShort;
		if (type == "UInt"sv)			return ScriptFieldType::UInt;
		if (type == "ULong"sv)			return ScriptFieldType::ULong;
		if (type == "Float"sv)			return ScriptFieldType::Float;
		if (type == "Double"sv)			return ScriptFieldType::Double;
		if (type == "Bool"sv)			return ScriptFieldType::Bool;
		if (type == "Char"sv)			return ScriptFieldType::Char;
		if (type == "Byte"sv)			return ScriptFieldType::Byte;
		if (type == "String"sv)			return ScriptFieldType::String;
		if (type == "Vector2"sv)		return ScriptFieldType::Vec2;
		if (type == "Vector3"sv)		return ScriptFieldType::Vec3;
		if (type == "Vector4"sv)		return ScriptFieldType::Vec4;
		if (type == "MaterialRef"sv)	return ScriptFieldType::MaterialRef;
		if (type == "MeshRef"sv)		return ScriptFieldType::MeshRef;
		if (type == "TextureRef"sv)	return ScriptFieldType::TextureRef;
		if (type == "AssetHandle"sv)	return ScriptFieldType::AssetHandle;

		KBRAssert(false, "Unknown script field type!");
		return ScriptFieldType::Int;
	}
}