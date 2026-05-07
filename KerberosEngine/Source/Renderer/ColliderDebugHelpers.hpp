#pragma once

#include "Vertex.hpp"

#include <glm/glm.hpp>

#include <numbers>

namespace Kerberos
{
	class ColliderDebugHelpers
	{
	public:
		static LineVertex MakeLineVertex(const glm::vec3& position)
		{
			return { .Position = position, .Color = ColliderDebugColor };
		}

		static void AddLine(std::vector<LineVertex>& vertices, const glm::vec3& a, const glm::vec3& b)
		{
			vertices.push_back(MakeLineVertex(a));
			vertices.push_back(MakeLineVertex(b));
		}

		static void AddBoxLines(std::vector<LineVertex>& vertices, const glm::mat4& transform, const glm::vec3& halfExtents)
		{
			const std::array<glm::vec3, 8> corners = {
				glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z), glm::vec3(halfExtents.x, -halfExtents.y, -halfExtents.z),
				glm::vec3(halfExtents.x, halfExtents.y, -halfExtents.z),   glm::vec3(-halfExtents.x, halfExtents.y, -halfExtents.z),
				glm::vec3(-halfExtents.x, -halfExtents.y, halfExtents.z),  glm::vec3(halfExtents.x, -halfExtents.y, halfExtents.z),
				glm::vec3(halfExtents.x, halfExtents.y, halfExtents.z),    glm::vec3(-halfExtents.x, halfExtents.y, halfExtents.z)
			};
			std::array<glm::vec3, 8> worldCorners{};
			for (uint32_t i = 0; i < corners.size(); ++i)
			{
				worldCorners[i] = glm::vec3(transform * glm::vec4(corners[i], 1.0f));
			}

			constexpr std::array<std::pair<uint32_t, uint32_t>, 12> edges = {
				std::pair{0, 1}, std::pair{1, 2}, std::pair{2, 3}, std::pair{3, 0},
				std::pair{4, 5}, std::pair{5, 6}, std::pair{6, 7}, std::pair{7, 4},
				std::pair{0, 4}, std::pair{1, 5}, std::pair{2, 6}, std::pair{3, 7}
			};
			for (const auto& [a, b] : edges)
			{
				AddLine(vertices, worldCorners[a], worldCorners[b]);
			}
		}

		static void AddCircleLines(std::vector<LineVertex>& vertices, const glm::mat4& transform, const int axisA, const int axisB, const float radius, const uint32_t segments = 32)
		{
			glm::vec3 previous(0.0f);
			bool hasPrevious = false;

			for (uint32_t i = 0; i <= segments; ++i)
			{
				constexpr float twoPi = std::numbers::pi_v<float> *2.0f;
				const float t = static_cast<float>(i) / static_cast<float>(segments);
				const float angle = twoPi * t;
				glm::vec3 local(0.0f);
				local[axisA] = std::cos(angle) * radius;
				local[axisB] = std::sin(angle) * radius;
				const glm::vec3 current = glm::vec3(transform * glm::vec4(local, 1.0f));
				if (hasPrevious)
				{
					AddLine(vertices, previous, current);
				}
				previous = current;
				hasPrevious = true;
			}
		}

		static void AddCapsuleLines(std::vector<LineVertex>& vertices, const glm::mat4& transform, const float radius, const float halfHeight)
		{
			const glm::mat4 topCenter = transform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, halfHeight, 0.0f));
			const glm::mat4 bottomCenter = transform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -halfHeight, 0.0f));

			AddCircleLines(vertices, topCenter, 0, 2, radius);
			AddCircleLines(vertices, bottomCenter, 0, 2, radius);

			const std::array<glm::vec3, 4> sidePoints = {
				glm::vec3(radius, 0.0f, 0.0f), glm::vec3(-radius, 0.0f, 0.0f),
				glm::vec3(0.0f, 0.0f, radius), glm::vec3(0.0f, 0.0f, -radius)
			};
			for (const glm::vec3& point : sidePoints)
			{
				const glm::vec3 top = glm::vec3(topCenter * glm::vec4(point, 1.0f));
				const glm::vec3 bottom = glm::vec3(bottomCenter * glm::vec4(point, 1.0f));
				AddLine(vertices, top, bottom);
			}
		}

	public:
		constexpr static glm::vec3 ColliderDebugColor{0.0f, 1.0f, 0.0f};
		constexpr static uint32_t ColliderDebugMaxVertices = 65536;
	};
}