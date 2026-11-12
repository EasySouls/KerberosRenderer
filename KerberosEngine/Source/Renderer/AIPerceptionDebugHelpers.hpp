#pragma once

#include "ColliderDebugHelpers.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace Kerberos
{
	/// Generates the line geometry for the AI perception debug view. The helper
	/// is UI-free and Scene-free so it can be covered by engine unit tests.
	class AIPerceptionDebugHelpers
	{
	public:
		constexpr static glm::vec3 SightColor{ 0.25f, 0.95f, 0.35f };
		constexpr static glm::vec3 LoseColor{ 1.0f, 0.62f, 0.15f };
		constexpr static glm::vec3 EyeColor{ 0.55f, 0.75f, 1.0f };

		/// Appends the perception footprint at the agent's feet, the eye-height
		/// marker, and one sector per radius. The forward vector is flattened to
		/// the XZ plane (matching the flat perception distance test).
		static void AppendConeLines(std::vector<LineVertex>& vertices,
		                            const glm::vec3& feet,
		                            const glm::vec3& forward,
		                            const float sightRadius,
		                            const float loseRadius,
		                            const float fovRadians,
		                            const float eyeHeight,
		                            const uint32_t arcSegments = 24)
		{
			glm::vec3 flatForward(forward.x, 0.0f, forward.z);
			if (glm::dot(flatForward, flatForward) <= 1.0e-6f)
				flatForward = glm::vec3(0.0f, 0.0f, 1.0f);
			flatForward = glm::normalize(flatForward);

			const glm::vec3 ground = feet + glm::vec3(0.0f, 0.02f, 0.0f);
			const glm::vec3 eye = feet + glm::vec3(0.0f, eyeHeight, 0.0f);
			ColliderDebugHelpers::AddLine(vertices, feet, eye, EyeColor);

			const uint32_t segments = std::max(arcSegments, 2u);

			if (fovRadians >= glm::radians(359.9f))
			{
				if (sightRadius > 0.0f)
					AddCircle(vertices, ground, sightRadius, SightColor, segments);
				if (loseRadius > 0.0f)
					AddCircle(vertices, ground, loseRadius, LoseColor, segments);
				return;
			}

			const float clampedFov = std::clamp(fovRadians, 0.0f, glm::radians(359.9f));
			const float halfFov = clampedFov * 0.5f;

			if (sightRadius > 0.0f)
			{
				AddArc(vertices, ground, flatForward, sightRadius, -halfFov, halfFov,
				       SightColor, segments);
				ColliderDebugHelpers::AddLine(vertices, ground,
				        ground + Direction(flatForward, -halfFov) * sightRadius, SightColor);
				ColliderDebugHelpers::AddLine(vertices, ground,
				        ground + Direction(flatForward, halfFov) * sightRadius, SightColor);
				ColliderDebugHelpers::AddLine(vertices, ground, ground + flatForward * sightRadius, SightColor);
			}

			if (loseRadius > 0.0f)
			{
				AddArc(vertices, ground, flatForward, loseRadius, -halfFov, halfFov,
				       LoseColor, segments);
				ColliderDebugHelpers::AddLine(vertices, ground,
				        ground + Direction(flatForward, -halfFov) * loseRadius, LoseColor);
				ColliderDebugHelpers::AddLine(vertices, ground,
				        ground + Direction(flatForward, halfFov) * loseRadius, LoseColor);
			}
		}

	private:
		static glm::vec3 Direction(const glm::vec3& flatForward, const float angle)
		{
			const float c = std::cos(angle);
			const float s = std::sin(angle);
			return glm::vec3(flatForward.x * c + flatForward.z * s,
			                 0.0f,
			                 -flatForward.x * s + flatForward.z * c);
		}

		static void AddArc(std::vector<LineVertex>& vertices,
		                   const glm::vec3& center,
		                   const glm::vec3& flatForward,
		                   const float radius,
		                   const float startAngle,
		                   const float endAngle,
		                   const glm::vec3& color,
		                   const uint32_t segments)
		{
			glm::vec3 previous = center + Direction(flatForward, startAngle) * radius;
			for (uint32_t i = 1; i <= segments; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(segments);
				const float angle = startAngle + (endAngle - startAngle) * t;
				const glm::vec3 current = center + Direction(flatForward, angle) * radius;
				ColliderDebugHelpers::AddLine(vertices, previous, current, color);
				previous = current;
			}
		}

		static void AddCircle(std::vector<LineVertex>& vertices,
		                      const glm::vec3& center,
		                      const float radius,
		                      const glm::vec3& color,
		                      const uint32_t segments)
		{
			constexpr float twoPi = std::numbers::pi_v<float> * 2.0f;
			glm::vec3 previous = center + glm::vec3(radius, 0.0f, 0.0f);
			for (uint32_t i = 1; i <= segments; ++i)
			{
				const float angle = twoPi * static_cast<float>(i) /
				                    static_cast<float>(segments);
				const glm::vec3 current = center + glm::vec3(
					std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
				ColliderDebugHelpers::AddLine(vertices, previous, current, color);
				previous = current;
			}
		}
	};
}
