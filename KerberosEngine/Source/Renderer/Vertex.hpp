#pragma once

#include "Core/Buffer.h"
#include "BufferLayout.hpp"
#include "Shaders/ShaderDataType.hpp"

#include "Vulkan.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

namespace Kerberos
{
	struct Vertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		glm::vec3 Tangent{ 0.0f };
		glm::vec2 TexCoord{ 0.0f };

		static BufferLayout GetLayout()
		{
			return BufferLayout
			{
				{ ShaderDataType::Float3, "Position" },
				{ ShaderDataType::Float3, "Normal"   },
				{ ShaderDataType::Float3, "Tangent"  },
				{ ShaderDataType::Float2, "TexCoord" },
			};
		}

		static vk::VertexInputBindingDescription GetBindingDescription() {
			return { 0, sizeof(Vertex), vk::VertexInputRate::eVertex };
		}

		static std::array<vk::VertexInputAttributeDescription, 4> GetAttributeDescriptions() {
			return {
				vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position)),
				vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal)),
				vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Tangent)),
				vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, TexCoord)),
			};
		}

		bool operator==(const Vertex& other) const {
			return Position == other.Position && Normal == other.Normal && Tangent == other.Tangent && TexCoord == other.TexCoord;
		}
	};

	struct TextVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
		glm::vec2 TexCoord;

		int EntityID = -1;

		static BufferLayout GetLayout()
		{
			return BufferLayout
			{
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float4, "a_Color"    },
				{ ShaderDataType::Float2, "a_TexCoord" },
				{ ShaderDataType::Int,	"a_EntityID" }
			};
		}

		static vk::VertexInputBindingDescription GetBindingDescription() {
			return { 0, sizeof(TextVertex), vk::VertexInputRate::eVertex };
		}

		static std::array<vk::VertexInputAttributeDescription, 4> GetAttributeDescriptions() {
			return {
				vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(TextVertex, Position)),
				vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(TextVertex, Color)),
				vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(TextVertex, TexCoord)),
				vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32Sint, offsetof(TextVertex, EntityID)),
			};
		}

		bool operator==(const TextVertex& other) const {
			return Position == other.Position && Color == other.Color && TexCoord == other.TexCoord && EntityID == other.EntityID;
		}
	};
}
