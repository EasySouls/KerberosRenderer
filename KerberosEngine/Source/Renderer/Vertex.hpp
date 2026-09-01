#pragma once

#include "BufferLayout.hpp"
#include "Shaders/ShaderDataType.hpp"

#include "Vulkan.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

namespace Kerberos
{
	struct Vertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		glm::vec4 Tangent{ 0.0f };
		glm::vec2 TexCoord{ 0.0f };

		static BufferLayout GetLayout()
		{
			return BufferLayout
			{
				{ ShaderDataType::Float3, "Position" },
				{ ShaderDataType::Float3, "Normal"   },
				{ ShaderDataType::Float4, "Tangent"  },
				{ ShaderDataType::Float2, "TexCoord" },
			};
		}

		static vk::VertexInputBindingDescription GetBindingDescription() {
			return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
		}

		static std::array<vk::VertexInputAttributeDescription, 4> GetAttributeDescriptions() {
			return {
				vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position)),
				vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal)),
				vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Tangent)),
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
			return { .binding = 0, .stride = sizeof(TextVertex), .inputRate = vk::VertexInputRate::eVertex };
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

	struct LineVertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Color{ 0.0f, 1.0f, 0.0f };

		static vk::VertexInputBindingDescription GetBindingDescription()
		{
			return { .binding = 0, .stride = sizeof(LineVertex), .inputRate = vk::VertexInputRate::eVertex };
		}

		static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions()
		{
			return {
				vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(LineVertex, Position)),
				vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(LineVertex, Color))
			};
		}
	};
}

template<>
struct std::hash<Kerberos::Vertex>
{
	size_t operator()(const Kerberos::Vertex& v) const noexcept
	{
		size_t seed = 0;
		const auto combine = [&seed]<typename T>(const T& value)
		{
			seed ^= std::hash<std::decay_t<T>>{}(value)
				+0x9e3779b9 + (seed << 6) + (seed >> 2);
		};

		combine(v.Position);
		combine(v.Normal);
		combine(v.Tangent);
		combine(v.TexCoord);
		return seed;
	}
};

template<>
struct std::hash<Kerberos::TextVertex>
{
	size_t operator()(const Kerberos::TextVertex& v) const noexcept
	{
		size_t seed = 0;
		const auto combine = [&seed]<typename T>(const T& value)
		{
			seed ^= std::hash<std::decay_t<T>>{}(value)
				+0x9e3779b9 + (seed << 6) + (seed >> 2);
		};

		combine(v.Position);
		combine(v.Color);
		combine(v.TexCoord);
		return seed;
	}
};

template<>
struct std::hash<Kerberos::LineVertex>
{
	size_t operator()(const Kerberos::LineVertex& v) const noexcept
	{
		size_t seed = 0;
		const auto combine = [&seed]<typename T>(const T & value)
		{
			seed ^= std::hash<std::decay_t<T>>{}(value)
				+0x9e3779b9 + (seed << 6) + (seed >> 2);
		};
		combine(v.Position);
		combine(v.Color);
		return seed;
	}
};