#pragma once

#include "Core/Core.hpp"
#include "Scene/Scene.hpp"

#include <filesystem>
#include <string>

namespace YAML { class Node; }

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

		/// Serializes an entity and all of its descendants into a self-contained blob
		/// so a destroyed subtree can be restored later.
		static std::string SerializeEntitySubtree(const Ref<Scene>& scene, const Entity& entity);

		/// Restores a blob produced by SerializeEntitySubtree and returns the root entity.
		static Entity DeserializeEntitySubtree(const Ref<Scene>& scene, const std::string& data);

	private:
		/// Creates one entity and its components from a serialized node. Shared by
		/// whole-scene loading and subtree restoration.
		static Entity DeserializeEntity(const Ref<Scene>& scene, const YAML::Node& node);

		Ref<Scene> m_Scene;
	};
}
