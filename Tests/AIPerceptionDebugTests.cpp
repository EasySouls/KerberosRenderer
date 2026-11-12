#include "Renderer/AIPerceptionDebugHelpers.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/epsilon.hpp>

#include <vector>

using namespace Kerberos;

namespace {

bool SameColor(const glm::vec3& a, const glm::vec3& b)
{
    return glm::all(glm::epsilonEqual(a, b, 1.0e-5f));
}

float FlatDistance(const glm::vec3& position, const glm::vec3& center)
{
    const glm::vec2 offset(position.x - center.x, position.z - center.z);
    return glm::length(offset);
}

} // namespace

TEST(AIPerceptionDebugTest, SectorStaysOnConfiguredRadii)
{
    constexpr glm::vec3 feet(10.0f, 2.0f, -4.0f);
    constexpr glm::vec3 forward(0.0f, 0.0f, 1.0f);

    std::vector<LineVertex> vertices;
    AIPerceptionDebugHelpers::AppendConeLines(
        vertices, feet, forward, 5.0f, 8.0f, glm::radians(90.0f), 1.5f, 8);

    ASSERT_FALSE(vertices.empty());

    bool foundSight = false;
    bool foundLose = false;
    bool foundEye = false;

    for (const LineVertex& vertex : vertices)
    {
        const float distance = FlatDistance(vertex.Position, feet);
        if (SameColor(vertex.Color, AIPerceptionDebugHelpers::SightColor))
        {
            foundSight = true;
            EXPECT_TRUE(distance < 1.0e-4f || std::abs(distance - 5.0f) < 1.0e-4f);
        }
        else if (SameColor(vertex.Color, AIPerceptionDebugHelpers::LoseColor))
        {
            foundLose = true;
            EXPECT_TRUE(distance < 1.0e-4f || std::abs(distance - 8.0f) < 1.0e-4f);
        }
        else if (SameColor(vertex.Color, AIPerceptionDebugHelpers::EyeColor))
        {
            foundEye = true;
            EXPECT_TRUE(std::abs(vertex.Position.y - feet.y) < 1.0e-4f ||
                        std::abs(vertex.Position.y - (feet.y + 1.5f)) < 1.0e-4f);
        }
    }

    EXPECT_TRUE(foundSight);
    EXPECT_TRUE(foundLose);
    EXPECT_TRUE(foundEye);
}

TEST(AIPerceptionDebugTest, FullCircleUsesOnlyRadii)
{
    constexpr glm::vec3 feet(0.0f, 0.0f, 0.0f);
    constexpr glm::vec3 forward(0.0f, 0.0f, 1.0f);

    std::vector<LineVertex> vertices;
    AIPerceptionDebugHelpers::AppendConeLines(
        vertices, feet, forward, 3.0f, 6.0f, glm::radians(360.0f), 1.0f, 12);

    ASSERT_FALSE(vertices.empty());

    for (const LineVertex& vertex : vertices)
    {
        if (SameColor(vertex.Color, AIPerceptionDebugHelpers::SightColor))
            EXPECT_NEAR(FlatDistance(vertex.Position, feet), 3.0f, 1.0e-4f);
        else if (SameColor(vertex.Color, AIPerceptionDebugHelpers::LoseColor))
            EXPECT_NEAR(FlatDistance(vertex.Position, feet), 6.0f, 1.0e-4f);
    }
}

TEST(AIPerceptionDebugTest, DegenerateForwardDoesNotProduceInvalidPositions)
{
    constexpr glm::vec3 feet(1.0f, 0.0f, 2.0f);

    std::vector<LineVertex> vertices;
    AIPerceptionDebugHelpers::AppendConeLines(
        vertices, feet, glm::vec3(0.0f, -1.0f, 0.0f), 4.0f, 7.0f,
        glm::radians(45.0f), 1.2f, 6);

    ASSERT_FALSE(vertices.empty());
    for (const LineVertex& vertex : vertices)
    {
        EXPECT_TRUE(std::isfinite(vertex.Position.x));
        EXPECT_TRUE(std::isfinite(vertex.Position.y));
        EXPECT_TRUE(std::isfinite(vertex.Position.z));
    }
}
