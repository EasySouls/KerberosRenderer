#pragma once

#include "Assets/Asset.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Kerberos 
{

struct ParticleEmitterComponent
{
	bool IsActive = true;
	float SpawnRate = 10.0f; // Particles per second
	float MinLifetime = 1.0f; // Minimum lifetime of particles in seconds
	float MaxLifetime = 5.0f; // Maximum lifetime of particles in seconds

	glm::vec3 MinVelocity = glm::vec3(-1.0f, 0.0f, -1.0f); // Minimum initial velocity of particles
	glm::vec3 MaxVelocity = glm::vec3(1.0f, 5.0f, 1.0f); // Maximum initial velocity of particles
	glm::vec3 MinAcceleration = glm::vec3(0.0f, -9.81f, 0.0f); // Minimum acceleration of particles
	glm::vec3 MaxAcceleration = glm::vec3(0.0f, -9.81f, 0.0f); // Maximum acceleration of particles
	glm::vec4 StartColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // Initial color of particles
	glm::vec4 EndColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f); // Final color of particles
	float StartSize = 0.1f; // Initial size of particles
	float EndSize = 0.5f; // Final size of particles
	glm::vec2 SubUVGrid = glm::vec2(1, 1); // SubUV grid dimensions (columns, rows) for texture animation

	AssetHandle ParticleTexture = AssetHandle::Invalid(); // Handle to the particle texture

	float spawnAccumulator = 0.0f; // Internal counter for fractional particle spawning
};


}