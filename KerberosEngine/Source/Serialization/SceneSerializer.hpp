#pragma once

#include "Core/Core.hpp"
#include "Scene/Scene.hpp"

#include <filesystem>

namespace Kerberos
{
	class SceneSerializer
	{
	public:
		explicit SceneSerializer(const Ref<Scene>& scene)
			: m_Scene(scene) {
		}

		void Serialize(const std::filesystem::path& filepath) const;
		void SerializeRuntime(const std::filesystem::path& filepath);

		bool Deserialize(const std::filesystem::path& filepath) const;
		bool DeserializeRuntime(const std::filesystem::path& filepath) const;

	private:
		Ref<Scene> m_Scene;
	};
}
