#pragma once

#include "Core/Core.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Kerberos 
{ 
	class Mesh; 
}

namespace Kerberos
{
	struct RigidBody3DComponent
	{

		enum class BodyType : std::uint8_t
		{
			Static,
			Dynamic,
			Kinematic
		};
		BodyType Type = BodyType::Dynamic;
		float Mass = 1.0f;
		glm::vec3 Velocity = glm::vec3(0.0f);
		glm::vec3 AngularVelocity = glm::vec3(0.0f);
		bool UseGravity = true;
		/// Between 0 and 1
		float Friction = 0.5f;
		/// Between 0 and 1
		float Restitution = 0.5f;

		/// Pointer to the physics engine's runtime body
		void* RuntimeBody = nullptr;

		/// TODO: add a physics body id, so when not interacting directly with the runtime body we can just use the id to send data to the physics engine

		bool IsDirty = true;

		RigidBody3DComponent() = default;
		explicit RigidBody3DComponent(const BodyType type)
			: Type(type)
		{
		}

        RigidBody3DComponent(const RigidBody3DComponent&) = default;
        RigidBody3DComponent(RigidBody3DComponent&&) = default;
        RigidBody3DComponent& operator=(const RigidBody3DComponent&) = default;
        RigidBody3DComponent& operator=(RigidBody3DComponent&&) = default;
	};

	struct BoxCollider3DComponent
	{
		glm::vec3 Size = glm::vec3(1.0f);
		glm::vec3 Offset = glm::vec3(0.0f);
		bool IsTrigger = false;

		// Pointer to the physics engine's runtime collider
		void* RuntimeCollider = nullptr;

		BoxCollider3DComponent() = default;
		explicit BoxCollider3DComponent(const glm::vec3& size, const glm::vec3& offset = glm::vec3(0.0f), const bool isTrigger = false)
			: Size(size), Offset(offset), IsTrigger(isTrigger)
		{
		}

		BoxCollider3DComponent(const BoxCollider3DComponent&) = default;
        BoxCollider3DComponent(BoxCollider3DComponent&&) = default;
        BoxCollider3DComponent& operator=(const BoxCollider3DComponent&) = default;
        BoxCollider3DComponent& operator=(BoxCollider3DComponent&&) = default;
	};

	struct SphereCollider3DComponent
	{
		float Radius = 1.0f;
		glm::vec3 Offset = glm::vec3(0.0f);
		bool IsTrigger = false;

		// Pointer to the physics engine's runtime collider
		void* RuntimeCollider = nullptr;

		SphereCollider3DComponent() = default;
		explicit SphereCollider3DComponent(const float radius, const glm::vec3& offset = glm::vec3(0.0f), const bool isTrigger = false)
			: Radius(radius), Offset(offset), IsTrigger(isTrigger)
		{
		}

		SphereCollider3DComponent(const SphereCollider3DComponent&) = default;
        SphereCollider3DComponent(SphereCollider3DComponent&&) = default;
        SphereCollider3DComponent& operator=(const SphereCollider3DComponent&) = default;
        SphereCollider3DComponent& operator=(SphereCollider3DComponent&&) = default;
	};

	struct CapsuleCollider3DComponent
	{
		float Radius = 0.5f;
		float Height = 1.0f;
		glm::vec3 Offset = glm::vec3(0.0f);
		bool IsTrigger = false;

		// Pointer to the physics engine's runtime collider
		void* RuntimeCollider = nullptr;

		CapsuleCollider3DComponent() = default;
		explicit CapsuleCollider3DComponent(const float radius, const float height, const glm::vec3& offset = glm::vec3(0.0f), const bool isTrigger = false)
			: Radius(radius), Height(height), Offset(offset), IsTrigger(isTrigger)
		{
		}

		CapsuleCollider3DComponent(const CapsuleCollider3DComponent&) = default;
        CapsuleCollider3DComponent(CapsuleCollider3DComponent&&) = default;
        CapsuleCollider3DComponent& operator=(const CapsuleCollider3DComponent&) = default;
        CapsuleCollider3DComponent& operator=(CapsuleCollider3DComponent&&) = default;
	};

	struct MeshCollider3DComponent
	{
		Ref<Mesh> Mesh = nullptr;
		glm::vec3 Offset = glm::vec3(0.0f);
		bool IsTrigger = false;
		// Pointer to the physics engine's runtime collider
		void* RuntimeCollider = nullptr;

		MeshCollider3DComponent() = default;
		explicit MeshCollider3DComponent(const Ref<Kerberos::Mesh>& mesh, const glm::vec3& offset = glm::vec3(0.0f), const bool isTrigger = false)
			: Mesh(mesh), Offset(offset), IsTrigger(isTrigger)
		{
		}

		MeshCollider3DComponent(const MeshCollider3DComponent&) = default;
        MeshCollider3DComponent(MeshCollider3DComponent&&) = default;
        MeshCollider3DComponent& operator=(const MeshCollider3DComponent&) = default;
        MeshCollider3DComponent& operator=(MeshCollider3DComponent&&) = default;
	};
}