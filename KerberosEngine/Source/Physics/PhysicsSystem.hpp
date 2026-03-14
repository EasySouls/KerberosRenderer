#pragma once

#include "Core/Core.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Kerberos
{
	class Scene;

	class PhysicsSystem
	{
	public:
		PhysicsSystem() = default;
		virtual ~PhysicsSystem() = default;

		PhysicsSystem(const PhysicsSystem& other) = default;
		PhysicsSystem(PhysicsSystem&& other) noexcept = delete;

		PhysicsSystem& operator=(const PhysicsSystem& other) = delete;
		PhysicsSystem& operator=(PhysicsSystem&& other) noexcept = delete;

		virtual void Initialize(const Ref<Scene>& scene) = 0;
		virtual void Cleanup() = 0;

		virtual void Update(float deltaTime) = 0;

		virtual void AddImpulse(std::uint32_t bodyId, const glm::vec3& impulse) const = 0;
		virtual void AddImpulse(std::uint32_t bodyId, const glm::vec3& impulse, const glm::vec3& point) const = 0;
	};
}