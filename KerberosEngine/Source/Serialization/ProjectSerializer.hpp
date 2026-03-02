#pragma once

#include "Core/Core.hpp"
#include "Project/Project.hpp"

namespace Kerberos
{
	class ProjectSerializer
	{
	public:
		explicit ProjectSerializer(const Ref<Project>& project);

		/**
		* Serializes the project to the specified file path.
		* @param filepath The path where the project file should be saved.
		* @return true if serialization was successful, false otherwise.
		*/
		bool Serialize(const std::filesystem::path& filepath) const;

		/**
		* Deserializes a project from the specified file path.
		* @param filepath The path to the project file to load.
		* @return true if deserialization was successful, false otherwise.
		*/
		bool Deserialize(const std::filesystem::path& filepath) const;

	private:
		Ref<Project> m_Project;
	};
}