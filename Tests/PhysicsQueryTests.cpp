#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <gtest/gtest.h>

import Kerberos;

namespace
{
	Kerberos::Entity CreateBoxEntity(const Kerberos::Ref<Kerberos::Scene>& scene, const char* name,
	                                 const glm::vec3& position)
	{
		Kerberos::Entity entity = scene->CreateEntity(name);
		entity.GetComponent<Kerberos::TransformComponent>().Translation = position;

		auto& rigidBody = entity.AddComponent<Kerberos::RigidBody3DComponent>();
		rigidBody.Type = Kerberos::RigidBody3DComponent::BodyType::Static;

		auto& collider = entity.AddComponent<Kerberos::BoxCollider3DComponent>();
		collider.Size = glm::vec3(1.0f);

		return entity;
	}
}

TEST(PhysicsQueryTest, OverlapBoxAndSphereFindEntities)
{
	const Kerberos::Ref<Kerberos::Scene> scene = Kerberos::CreateRef<Kerberos::Scene>();

	const Kerberos::Entity centerCube = CreateBoxEntity(scene, "Center", glm::vec3(0.0f));
	const Kerberos::Entity farCube = CreateBoxEntity(scene, "Far", glm::vec3(10.0f, 0.0f, 0.0f));

	scene->CalculateEntityTransforms();
	scene->OnSimulationStart();
	scene->GetPhysicsSystem().Update(0.0f);

	const std::vector<Kerberos::OverlapHit> boxHits =
		scene->GetPhysicsSystem().OverlapBox(
			glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));

	ASSERT_FALSE(boxHits.empty());
	EXPECT_EQ(boxHits.front().EntityID, centerCube.GetUUID());
	EXPECT_EQ(boxHits.size(), 1u);

	const std::vector<Kerberos::OverlapHit> sphereHits =
		scene->GetPhysicsSystem().OverlapSphere(glm::vec3(10.0f, 0.0f, 0.0f), 2.0f);

	ASSERT_FALSE(sphereHits.empty());
	EXPECT_EQ(sphereHits.front().EntityID, farCube.GetUUID());

	const std::vector<Kerberos::OverlapHit> emptyHits =
		scene->GetPhysicsSystem().OverlapSphere(glm::vec3(0.0f), 0.0f);
	EXPECT_TRUE(emptyHits.empty());

	scene->OnSimulationStop();
}
