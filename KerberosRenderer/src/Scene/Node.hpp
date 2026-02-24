#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Mesh.hpp"
#include "Renderer/Material.hpp"

#include <memory>
#include <string>

namespace kbr 
{
	struct Node 
	{
		glm::vec3 Position{0.0f};
		glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{1.0f};

		Ref<Mesh> Mesh = nullptr;
		// TODO: store this on the mesh, or if it has submeshes, store per-submesh materials
		Ref<Material> Material = nullptr; 

		std::string Name;

		bool Visible = true;

		glm::mat4 GetTransform() const 
		{
			const glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
			const glm::mat4 rot = glm::toMat4(glm::quat(Rotation));
			const glm::mat4 scaling = glm::scale(glm::mat4(1.0f), Scale);
			return translation * rot * scaling;
		}
	};
} // namespace kbr