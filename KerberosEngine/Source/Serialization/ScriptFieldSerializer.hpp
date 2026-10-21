#pragma once

#include "Scripting/ScriptClass.hpp"
#include "SerializationUtils.hpp"

#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

namespace Kerberos::ScriptFieldSerializer
{
	/// Writes the `Data` key for a serialized script field.
	inline void Serialize(YAML::Emitter& out, const ScriptFieldInitializer& field)
	{
		switch (field.Field.Type)
		{
		case ScriptFieldType::Short:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<short>();
			break;
		case ScriptFieldType::Int:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<int>();
			break;
		case ScriptFieldType::Long:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<long>();
			break;
		case ScriptFieldType::UShort:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint16_t>();
			break;
		case ScriptFieldType::UInt:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint32_t>();
			break;
		case ScriptFieldType::ULong:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint64_t>();
			break;
		case ScriptFieldType::Float:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<float>();
			break;
		case ScriptFieldType::Double:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<double>();
			break;
		case ScriptFieldType::Bool:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<bool>();
			break;
		case ScriptFieldType::Char:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<char>();
			break;
		case ScriptFieldType::Byte:
			out << YAML::Key << "Data" << YAML::Value << static_cast<int>(field.GetValue<uint8_t>());
			break;
		case ScriptFieldType::String:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<std::string>();
			break;
		case ScriptFieldType::Vec2:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<glm::vec2>();
			break;
		case ScriptFieldType::Vec3:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<glm::vec3>();
			break;
		case ScriptFieldType::Vec4:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<glm::vec4>();
			break;
		case ScriptFieldType::MaterialRef:
		case ScriptFieldType::MeshRef:
		case ScriptFieldType::TextureRef:
		case ScriptFieldType::AssetHandle:
		case ScriptFieldType::PrefabRef:
		case ScriptFieldType::EntityRef:
			out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint64_t>();
			break;
		}
	}

	/// Reads the `Data` key of a serialized script field into the initializer.
	inline void Deserialize(const YAML::Node& node, ScriptFieldInitializer& initializer)
	{
		switch (initializer.Field.Type)
		{
		case ScriptFieldType::Short:
			initializer.SetValue<short>(node["Data"].as<short>());
			break;
		case ScriptFieldType::Int:
			initializer.SetValue<int>(node["Data"].as<int>());
			break;
		case ScriptFieldType::Long:
			initializer.SetValue<long>(node["Data"].as<long>());
			break;
		case ScriptFieldType::UShort:
			initializer.SetValue<uint16_t>(node["Data"].as<uint16_t>());
			break;
		case ScriptFieldType::UInt:
			initializer.SetValue<uint32_t>(node["Data"].as<uint32_t>());
			break;
		case ScriptFieldType::ULong:
			initializer.SetValue<uint64_t>(node["Data"].as<uint64_t>());
			break;
		case ScriptFieldType::Float:
			initializer.SetValue<float>(node["Data"].as<float>());
			break;
		case ScriptFieldType::Double:
			initializer.SetValue<double>(node["Data"].as<double>());
			break;
		case ScriptFieldType::Bool:
			initializer.SetValue<bool>(node["Data"].as<bool>());
			break;
		case ScriptFieldType::Char:
			initializer.SetValue<char>(node["Data"].as<char>());
			break;
		case ScriptFieldType::Byte:
			initializer.SetValue<uint8_t>(static_cast<uint8_t>(node["Data"].as<int>()));
			break;
		case ScriptFieldType::String:
			initializer.SetValue<std::string>(node["Data"].as<std::string>());
			break;
		case ScriptFieldType::Vec2:
			initializer.SetValue<glm::vec2>(node["Data"].as<glm::vec2>());
			break;
		case ScriptFieldType::Vec3:
			initializer.SetValue<glm::vec3>(node["Data"].as<glm::vec3>());
			break;
		case ScriptFieldType::Vec4:
			initializer.SetValue<glm::vec4>(node["Data"].as<glm::vec4>());
			break;
		case ScriptFieldType::MaterialRef:
		case ScriptFieldType::MeshRef:
		case ScriptFieldType::TextureRef:
		case ScriptFieldType::AssetHandle:
		case ScriptFieldType::PrefabRef:
		case ScriptFieldType::EntityRef:
			initializer.SetValue<uint64_t>(node["Data"].as<uint64_t>());
			break;
		}
	}
}
