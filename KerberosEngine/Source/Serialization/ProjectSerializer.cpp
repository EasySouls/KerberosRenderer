#include "ProjectSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>

import Kerberos;

namespace Kerberos
{
	ProjectSerializer::ProjectSerializer(const Ref<Project>& project)
		: m_Project(project)
	{
	}

	bool ProjectSerializer::Serialize(const std::filesystem::path& filepath) const
	{
		const auto& info = m_Project->GetInfo();

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Project" << YAML::Value;
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << info.Name;
			out << YAML::Key << "AssetDirectory" << YAML::Value << info.AssetDirectory.string();
			out << YAML::Key << "StartScenePath" << YAML::Value << info.StartScenePath.string();
			out << YAML::EndMap;
		}
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		fout << out.c_str();

		if (fout.fail())
		{
			Log::CoreError("Failed to write project file to '{}'", filepath.string());
			return false;
		}

		return true;
	}

	bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath) const
	{
		auto& info = m_Project->GetInfo();

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath.string());
		}
		catch (YAML::ParserException& ex)
		{
			Log::CoreError("Failed to load .kerberos file '{}'\n\t{}", filepath.string(), ex.what());
			return false;
		}

		const auto projectNode = data["Project"];
		if (!projectNode)
		{
			Log::CoreError("Invalid project file");
			return false;
		}

		info.Name = projectNode["Name"].as<std::string>();
		info.AssetDirectory = projectNode["AssetDirectory"].as<std::string>();
		info.StartScenePath = projectNode["StartScenePath"].as<std::string>();

		return true;
	}
}